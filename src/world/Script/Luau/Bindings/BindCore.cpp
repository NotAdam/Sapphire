#include "Bindings.h"

#include "../LuauContext.h"
#include "../LuauRuntime.h"
#include "../LuauScheduler.h"

#include <Logging/Logger.h>
#include <Random/RNGMgr.h>
#include <Service.h>

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

namespace Sapphire::Scripting::Bindings
{
  LuauScheduler& scheduler( lua_State* L )
  {
    auto* rt = LuauRuntime::from( L );
    if( !rt || !rt->scheduler() )
      luaL_error( L, "the Luau scheduler is not running" );
    return *rt->scheduler();
  }

  ScriptThread* callingThread( lua_State* L )
  {
    auto* rt = LuauRuntime::from( L );
    return rt && rt->scheduler() ? rt->scheduler()->current() : nullptr;
  }

  LuauContext& callingContext( lua_State* L )
  {
    auto* t = callingThread( L );
    if( !t || !t->ctx )
      luaL_error( L, "this function can only be called from a script" );
    return *t->ctx;
  }

  ScriptThread& beginYield( lua_State* L )
  {
    auto* t = callingThread( L );
    if( !t || t->co != L )
      luaL_error( L, "yielding host functions must be called from a task thread, not inside coroutine.create/wrap" );
    if( t->sync )
      luaL_error( L, "%s", t->syncError );
    return *t;
  }

  namespace
  {
    ////////////////////////////////////////////////////////////////////////////////////////////// host primitives

    int l_spawn( lua_State* L )
    {
      luaL_checktype( L, 1, LUA_TFUNCTION );
      auto* caller = callingThread( L );
      if( !caller || !caller->ctx )
        luaL_error( L, "task.spawn can only be called from a script" );
      auto& sched = scheduler( L );
      if( caller->ctx->isClosing() )
        luaL_error( L, "task.spawn: the script is being unloaded" );
      // top-level code runs in a load probe (also on every reload, next to the still running old version)
      if( caller->ctx->discovery() )
        luaL_error( L, "task.spawn/task.delay cannot be used in top-level script code; start tasks from onStart, "
                       "onCreate or another handler" );
      int n = lua_gettop( L );
      auto* t = sched.create( *caller->ctx, caller->playerId );
      lua_xmove( L, t->co, n );
      uint32_t id = t->id;
      sched.start( *t, n - 1 );
      lua_pushnumber( L, id );
      return 1;
    }

    int l_park( lua_State* L )
    {
      auto& t = beginYield( L );
      t.wait = WaitKind::Park;
      return lua_yield( L, 0 );
    }

    // absolute wake time on the thread's context clock, seconds from now; UINT64_MAX = never
    uint64_t deadline( const ScriptThread& t, double seconds )
    {
      if( std::isnan( seconds ) || seconds < 0 )
        seconds = 0;
      double ms = seconds * 1000.0;
      uint64_t now = t.ctx->clockMs();
      // 18446744073709551616.0 == 2^64: anything at or above it does not fit a uint64
      if( ms >= 18446744073709551616.0 )
        return UINT64_MAX;
      uint64_t delay = static_cast< uint64_t >( ms );
      return delay >= UINT64_MAX - now ? UINT64_MAX : now + delay;
    }

    int l_wait( lua_State* L )
    {
      double seconds = luaL_optnumber( L, 1, 0 );
      auto& t = beginYield( L );
      if( !t.ctx )
        luaL_error( L, "the script was unloaded" );
      t.wait = WaitKind::Timer;
      t.wakeAt = deadline( t, seconds );
      return lua_yield( L, 0 );
    }

    // (timeout: number?, recheck: boolean?): parks until notifyChange for the context, or until the timeout.
    // recheck = true: the segment only re-evaluated a predicate since the last wake, so its end notifies nobody
    int l_waitChange( lua_State* L )
    {
      bool timed = !lua_isnoneornil( L, 1 );
      double seconds = timed ? luaL_checknumber( L, 1 ) : 0;
      bool recheck = lua_toboolean( L, 2 ) != 0;
      auto& t = beginYield( L );
      if( !t.ctx )
        luaL_error( L, "the script was unloaded" );
      t.wait = WaitKind::Change;
      t.wakeAt = timed ? deadline( t, seconds ) : UINT64_MAX;
      t.recheck = recheck;
      return lua_yield( L, 0 );
    }

    // returns the registration's serial, for off_exit
    int l_onExit( lua_State* L )
    {
      luaL_checktype( L, 1, LUA_TFUNCTION );
      auto* t = callingThread( L );
      if( !t )
        luaL_error( L, "task.onExit can only be called from a script" );
      lua_pushnumber( L, static_cast< double >( scheduler( L ).addExit( *t, L, 1 ) ) );
      return 1;
    }

    // (serial, thread?): drops a registration of the given (default: calling) thread that has not run yet, also while
    // that thread is running its exit callbacks; no-op when it is not (or no longer) registered there
    int l_offExit( lua_State* L )
    {
      auto serial = Bind::Stack< uint64_t >::check( L, 1 );
      uint32_t threadId = 0;
      if( lua_isnoneornil( L, 2 ) )
      {
        auto* t = callingThread( L );
        if( !t )
          return 0;
        threadId = t->id;
      }
      else
        threadId = Bind::Stack< uint32_t >::check( L, 2 );
      scheduler( L ).removeExit( threadId, serial );
      return 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////// script / on

    void declareKind( lua_State* L, ScriptKind kind, uint32_t id )
    {
      auto& ctx = callingContext( L );
      if( ctx.declaredKind() != ScriptKind::None )
        luaL_error( L, "script kind already declared" );
      uint32_t norm = normaliseScriptId( kind, id );
      if( !ctx.discovery() && ( ctx.kind() != kind || ctx.scriptId() != norm ) )
        luaL_error( L, "script declaration changed; reload required" );
      ctx.declare( kind, norm );
    }

    void addTalkTarget( lua_State* L, LuauContext& ctx, int targetIdx, int fnIdx, int depth )
    {
      targetIdx = lua_absindex( L, targetIdx );
      if( lua_type( L, targetIdx ) == LUA_TSTRING )
      {
        ctx.addTalkHandler( lua_tostring( L, targetIdx ), lua_ref( L, fnIdx ) );
        return;
      }
      if( lua_type( L, targetIdx ) != LUA_TTABLE )
        luaL_error( L, "on.talk: target must be an eobj name, an eobj reference or an array of them" );

      lua_rawgetfield( L, targetIdx, "name" );
      if( lua_type( L, -1 ) == LUA_TSTRING )
      {
        ctx.addTalkHandler( lua_tostring( L, -1 ), lua_ref( L, fnIdx ) );
        lua_pop( L, 1 );
        return;
      }
      lua_pop( L, 1 );

      if( depth > 0 )
        luaL_error( L, "on.talk: nested target lists are not supported" );
      int n = lua_objlen( L, targetIdx );
      if( n == 0 )
        luaL_error( L, "on.talk: empty target list" );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, targetIdx, i );
        addTalkTarget( L, ctx, -1, fnIdx, depth + 1 );
        lua_pop( L, 1 );
      }
    }

    int l_onTalk( lua_State* L )
    {
      auto& ctx = callingContext( L );
      if( ctx.declaredKind() != ScriptKind::Instance )
        luaL_error( L, "on.talk is only available in instance scripts" );
      luaL_checkany( L, 1 );
      luaL_checktype( L, 2, LUA_TFUNCTION );
      addTalkTarget( L, ctx, 1, 2, 0 );
      return 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////// require

    struct LoadingGuard
    {
      LuauContext& ctx;
      std::string key;
      ~LoadingGuard() { ctx.endLoading( key ); }
    };

    int l_require( lua_State* L )
    {
      auto& ctx = callingContext( L );
      std::string request = luaL_checkstring( L, 1 );
      auto& rt = ctx.runtime();

      fs::path base = rt.root();
      if( request.rfind( "./", 0 ) == 0 || request.rfind( "../", 0 ) == 0 )
      {
        // relative to the chunk of the nearest Luau caller (pcall( require, ... ) puts a C frame in between)
        lua_Debug ar{};
        for( int level = 1; lua_getinfo( L, level, "s", &ar ); ++level )
        {
          if( ar.source && ar.source[ 0 ] == '@' )
          {
            fs::path chunk( ar.source + 1 );
            base = ( chunk.is_absolute() ? chunk : rt.root() / chunk ).parent_path();
            break;
          }
        }
      }

      // fast path: a lexical key equals the stored key whenever the request is spelled like the file on disk
      fs::path file = ( base / ( request + ".luau" ) ).lexically_normal();
      std::string key = file.generic_string();
      std::error_code ec;

      auto pushCached = [ & ]( const std::string& k )
      {
        int cached = ctx.moduleRef( k );
        if( cached == LUA_NOREF )
          return false;
        if( cached == LUA_REFNIL )
          lua_pushnil( L );
        else
          lua_getref( L, cached );
        return true;
      };
      if( pushCached( key ) )
        return 1;

      if( !fs::exists( file, ec ) )
        luaL_error( L, "module not found: %s", request.c_str() );
      // the stored key is the on-disk spelling, matching LuauScriptMgr's directory-iteration keys under the canonical
      // root (Windows paths are case-insensitive). A symlinked file keeps its own name, as the iteration lists it
      if( fs::is_symlink( file, ec ) )
        file = fs::weakly_canonical( file.parent_path(), ec ) / file.filename();
      else
        file = fs::weakly_canonical( file, ec );
      if( ec )
        luaL_error( L, "module not found: %s", request.c_str() );
      key = file.generic_string();
      if( pushCached( key ) )
        return 1;
      if( !rt.inRoot( file ) )
        luaL_error( L, "module is outside the scripts directory: %s", request.c_str() );

      if( !ctx.beginLoading( key ) )
        luaL_error( L, "circular require: %s", request.c_str() );
      LoadingGuard guard{ ctx, key };

      ctx.addDependency( key );
      const auto& chunk = rt.compile( file );
      if( !chunk.error.empty() )
        luaL_error( L, "%s: %s", request.c_str(), chunk.error.c_str() );

      int top = lua_gettop( L );
      auto name = rt.chunkName( file );
      if( luau_load( L, name.c_str(), chunk.bytecode.data(), chunk.bytecode.size(), 0 ) != 0 )
        lua_error( L );
      lua_call( L, 0, LUA_MULTRET );
      if( lua_gettop( L ) - top != 1 )
        luaL_error( L, "module must return exactly one value: %s", request.c_str() );

      ctx.setModuleRef( key, lua_ref( L, -1 ) );
      return 1;
    }

    void logLine( lua_State* L, int level, const std::string& msg )
    {
      auto* t = callingThread( L );
      std::string who = t && t->ctx ? t->ctx->relPath() : std::string( "?" );
      switch( level )
      {
        case 0:
          Logger::debug( "[Luau] {}: {}", who, msg );
          break;
        case 1:
          Logger::info( "[Luau] {}: {}", who, msg );
          break;
        case 2:
          Logger::warn( "[Luau] {}: {}", who, msg );
          break;
        default:
          Logger::error( "[Luau] {}: {}", who, msg );
          break;
      }
    }

    uint32_t randomRange( uint32_t min, uint32_t max )
    {
      if( max < min )
        std::swap( min, max );
      if( !Common::Service< Common::Random::RNGMgr >::empty() )
        return Common::Service< Common::Random::RNGMgr >::ref().getRandGenerator< uint32_t >( min, max ).next();
      static std::mt19937 engine{ std::random_device{}() };
      return std::uniform_int_distribution< uint32_t >( min, max )( engine );
    }
  }

  void bindCore( Bind::Registry& reg )
  {
    Bind::Library( reg, "__host", true )
      .rawFunction( "spawn", l_spawn, "(fn: (...any) -> ...any, ...any) -> number" )
      .function( "current", []( lua_State* L ) -> double
      {
        auto* t = callingThread( L );
        if( !t )
          luaL_error( L, "not called from a task thread" );
        return t->id;
      } )
      .rawFunction( "park", l_park, "() -> ()" )
      .function( "wake", []( lua_State* L, uint32_t id ) { scheduler( L ).wake( id ); }, "id" )
      .function( "cancel", []( lua_State* L, uint32_t id ) { scheduler( L ).cancel( id ); }, "id" )
      .rawFunction( "wait", l_wait, "(seconds: number?) -> ()" )
      .rawFunction( "wait_change", l_waitChange, "(timeout: number?, recheck: boolean?) -> ()" )
      .function( "clock", []( lua_State* L ) -> double { return static_cast< double >( callingContext( L ).clockMs() ); } )
      .rawFunction( "on_exit", l_onExit, "(fn: (reason: string) -> ()) -> number" )
      .rawFunction( "off_exit", l_offExit, "(serial: number, thread: number?) -> ()" );

    Bind::Library( reg, "log" )
      .function( "debug", []( lua_State* L, std::string msg ) { logLine( L, 0, msg ); }, "msg" )
      .function( "info", []( lua_State* L, std::string msg ) { logLine( L, 1, msg ); }, "msg" )
      .function( "warn", []( lua_State* L, std::string msg ) { logLine( L, 2, msg ); }, "msg" )
      .function( "error", []( lua_State* L, std::string msg ) { logLine( L, 3, msg ); }, "msg" );

    Bind::Library( reg, "rng" )
      .function( "range", []( uint32_t min, uint32_t max ) { return randomRange( min, max ); }, "min, max" );

    Bind::Library( reg, "script" )
      .function( "instance", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Instance, id ); }, "id" )
      .function( "quest", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Quest, id ); }, "id" )
      .function( "event", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Event, id ); }, "id" )
      .function( "questBattle", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::QuestBattle, id ); }, "id" )
      .function( "action", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Action, id ); }, "id" )
      .function( "status", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Status, id ); }, "id" )
      .function( "zone", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::Zone, id ); }, "territoryTypeId" )
      .function( "eobj", []( lua_State* L, uint32_t id ) { declareKind( L, ScriptKind::EObj, id ); }, "baseId" );

    Bind::Library( reg, "on" )
      .rawFunction( "talk", l_onTalk,
                    "(target: string | EObjRef | {string | EObjRef}, fn: (player: Player, eobj: EObj, eventId: number) -> ()) -> ()" );

    // no type: editors keep their own require resolution (a declared type would turn every module into `any`)
    reg.global( "require", "", []( lua_State* L ) { lua_pushcfunction( L, l_require, "require" ); } );
  }
}
