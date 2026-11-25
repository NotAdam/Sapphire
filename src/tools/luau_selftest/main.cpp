// Selftest for the Luau scripting runtime (src/world/Script/Luau). Runs without a world: no services, no database.
// The LuauScriptMgr cases run a manager on a scratch scripts directory (singleton scripts only: no players, territories).
// usage: luau_selftest [cases dir]

#include <Script/Luau/Bindings/Bindings.h>
#include <Script/Luau/LuauBind.h>
#include <Script/Luau/LuauContext.h>
#include <Script/Luau/LuauRuntime.h>
#include <Script/Luau/LuauScheduler.h>
#include <Script/Luau/LuauScriptMgr.h>
#include <Script/Luau/LuauSnapshot.h>

#include <Logging/Logger.h>
#include <Util/Util.h>
#include <spdlog/spdlog.h>

#include <lua.h>
#include <luacode.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Sapphire::Scripting;
namespace fs = std::filesystem;

namespace
{
  struct StubHost : ISchedulerHost
  {
    int yields = 0;
    std::vector< ExitReason > exits;
    std::vector< std::string > errors;

    void onYield( ScriptThread& ) override { ++yields; }
    void onExit( ScriptThread&, ExitReason reason ) override { exits.push_back( reason ); }
    void reportError( ScriptThread&, const std::string& message ) override { errors.push_back( message ); }

    size_t cancelled() const
    {
      size_t n = 0;
      for( auto r : exits )
        n += r == ExitReason::Cancelled;
      return n;
    }

    bool errorContains( const std::string& a, const std::string& b = "" ) const
    {
      for( auto& e : errors )
        if( e.find( a ) != std::string::npos && ( b.empty() || e.find( b ) != std::string::npos ) )
          return true;
      return false;
    }
  };

  struct CounterObj
  {
    double value = 0;
  };

  using Fail = std::optional< std::string >;

  struct Env
  {
    LuauRuntime& rt;
    LuauScheduler& sched;
    StubHost& host;
    Sapphire::Scripting::Bind::Registry& reg;
    fs::path root;
    uint64_t base = 1000000;

    std::unique_ptr< LuauContext > open( const char* file, bool readOnly = false )
    {
      auto ctx = std::make_unique< LuauContext >( rt, sched, ScriptKind::None, 0, root / file );
      ctx->clock().nowMs = base;
      ctx->policy.readOnlyGlobals = readOnly;
      if( !ctx->open( false ) )
      {
        std::fprintf( stderr, "  open %s failed: %s\n", file, ctx->lastError().c_str() );
        for( auto& e : host.errors )
          std::fprintf( stderr, "  error: %s\n", e.c_str() );
        return nullptr;
      }
      return ctx;
    }

    std::optional< double > num( LuauContext& ctx, const char* name )
    {
      double v = 0;
      if( ctx.getGlobalNumber( name, v ) )
        return v;
      return std::nullopt;
    }

    Fail expect( LuauContext& ctx, const char* name, double want )
    {
      auto v = num( ctx, name );
      if( !v )
        return std::string( name ) + " is not set";
      if( *v != want )
        return std::string( name ) + " = " + std::to_string( *v ) + ", expected " + std::to_string( want );
      return std::nullopt;
    }

    void tick( LuauContext& ctx, uint64_t offset )
    {
      sched.updateTimers( ctx.clock(), base + offset );
      sched.drain();
    }
  };

#define CHECK( expr ) \
  do { if( auto f_ = ( expr ) ) return f_; } while( 0 )
#define REQUIRE( cond, msg ) \
  do { if( !( cond ) ) return std::string( msg ); } while( 0 )

  Fail caseTimer( Env& e )
  {
    auto ctx = e.open( "timer.luau" );
    REQUIRE( ctx, "open failed" );
    REQUIRE( ctx->startHandler( "go" ), "no go()" );
    e.tick( *ctx, 999 );
    CHECK( e.expect( *ctx, "x", 0 ) );
    e.tick( *ctx, 1000 );
    CHECK( e.expect( *ctx, "x", 1 ) );
    return std::nullopt;
  }

  Fail caseSpawnAwait( Env& e )
  {
    auto ctx = e.open( "spawn_await.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( !e.num( *ctx, "r" ), "r set before the timer fired" );
    e.tick( *ctx, 1000 );
    CHECK( e.expect( *ctx, "r", 7 ) );
    return std::nullopt;
  }

  Fail caseTaskCancel( Env& e )
  {
    auto ctx = e.open( "task_cancel.luau" );
    REQUIRE( ctx, "open failed" );
    auto before = e.host.cancelled();
    ctx->startHandler( "go" );
    e.sched.drain();
    CHECK( e.expect( *ctx, "okFlag", 0 ) );
    CHECK( e.expect( *ctx, "errIsCancelled", 1 ) );
    REQUIRE( e.host.cancelled() > before, "host did not see onExit( Cancelled )" );
    return std::nullopt;
  }

  Fail caseChangeWait( Env& e )
  {
    auto ctx = e.open( "change_wait.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( !e.num( *ctx, "done" ), "done set before the change" );
    ctx->setGlobalBool( "flag", true );
    // event-driven: time alone does not re-check the predicate
    e.tick( *ctx, 1000 );
    REQUIRE( !e.num( *ctx, "done" ), "wait.till re-checked without a change notification" );
    e.sched.notifyChange( *ctx );
    e.sched.drain();
    CHECK( e.expect( *ctx, "done", 1 ) );
    return std::nullopt;
  }

  Fail caseChangePoll( Env& e )
  {
    auto ctx = e.open( "change_poll.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    ctx->setGlobalBool( "flag", true );
    e.tick( *ctx, 50 );
    REQUIRE( !e.num( *ctx, "done" ), "done set after 50 ms" );
    e.tick( *ctx, 100 );
    CHECK( e.expect( *ctx, "done", 1 ) );
    return std::nullopt;
  }

  Fail caseChangeData( Env& e )
  {
    auto ctx = e.open( "change_data.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "idler" );
    ctx->startHandler( "waiter" );
    REQUIRE( !e.num( *ctx, "done" ), "done set before the setter ran" );
    ctx->startHandler( "setter" );
    CHECK( e.expect( *ctx, "done", 1 ) );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 1, "expected only the idler" );
    return std::nullopt;
  }

  Fail caseErrorIsolated( Env& e )
  {
    auto ctx = e.open( "error_isolated.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "bad" );
    ctx->startHandler( "good" );
    REQUIRE( e.host.errorContains( "boom", "error_isolated" ), "no error naming boom and the fixture" );
    CHECK( e.expect( *ctx, "ok", 1 ) );
    return std::nullopt;
  }

  Fail caseBudget( Env& e )
  {
    auto ctx = e.open( "budget.luau" );
    REQUIRE( ctx, "open failed" );
    e.rt.setTimeBudgetMs( 200 );
    auto start = std::chrono::steady_clock::now();
    ctx->startHandler( "go" );
    auto ms = std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - start ).count();
    e.rt.setTimeBudgetMs( 1000 );
    REQUIRE( ms < 1000, "endless loop ran for " + std::to_string( ms ) + " ms" );
    REQUIRE( e.host.errorContains( "200 ms time budget" ), "no time budget error" );
    return std::nullopt;
  }

  Fail caseRebuild( Env& e )
  {
    auto ctx = e.open( "rebuild.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 1, "expected one parked thread" );
    auto before = e.host.cancelled();
    ctx->close();
    REQUIRE( e.host.cancelled() == before + 1, "expected exactly one onExit( Cancelled )" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "threads left after close" );
    auto v2 = e.open( "rebuild_v2.luau" );
    REQUIRE( v2, "open v2 failed" );
    CHECK( e.expect( *v2, "v", 2 ) );
    return std::nullopt;
  }

  Fail caseSandbox( Env& e )
  {
    auto a = e.open( "sandbox.luau" );
    auto b = e.open( "sandbox_b.luau" );
    REQUIRE( a && b, "open failed" );
    CHECK( e.expect( *a, "g", 1 ) );
    CHECK( e.expect( *b, "seen", 1 ) );
    CHECK( e.expect( *b, "hostHidden", 1 ) );
    b->startHandler( "go" );
    CHECK( e.expect( *b, "frozen", 1 ) );
    return std::nullopt;
  }

  Fail caseCoroutineGuard( Env& e )
  {
    auto ctx = e.open( "coroutine_guard.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( e.host.errorContains( "task thread" ), "no task thread error" );
    ctx->startHandler( "resumeSelf" );
    e.tick( *ctx, 0 );
    CHECK( e.expect( *ctx, "resumeRejected", 1 ) );
    CHECK( e.expect( *ctx, "closeRejected", 1 ) );
    CHECK( e.expect( *ctx, "resumedBack", 1 ) );
    return std::nullopt;
  }

  Fail caseSpawnTopLevel( Env& e )
  {
    auto ctx = std::make_unique< LuauContext >( e.rt, e.sched, ScriptKind::None, 0, e.root / "spawn_toplevel.luau" );
    REQUIRE( !ctx->open( true ), "top-level task.spawn was accepted during discovery" );
    REQUIRE( e.host.errorContains( "top-level script code" ), "no top-level task.spawn error" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "threads left after the failed probe" );
    return std::nullopt;
  }

  Fail caseSyncReuse( Env& e )
  {
    auto ctx = e.open( "sync_reuse.luau" );
    REQUIRE( ctx, "open failed" );
    auto run = [ & ]( const char* fn, double arg, uint32_t playerId = 0 )
    {
      auto* t = e.sched.syncThread( *ctx, playerId );
      uint32_t id = t->id;
      ctx->pushGlobalFunction( t->co, fn );
      lua_pushnumber( t->co, arg );
      bool ok = e.sched.runSync( *t, 1 );
      return std::make_pair( id, ok );
    };
    auto [ id1, ok1 ] = run( "hook", 1 );
    auto [ id2, ok2 ] = run( "bad", 0 );
    auto [ id3, ok3 ] = run( "yielder", 0 );
    auto [ id4, ok4 ] = run( "hook", 2 );
    REQUIRE( ok1 && !ok2 && !ok3 && ok4, "unexpected runSync results" );
    REQUIRE( id1 == id2 && id2 == id3 && id3 == id4, "the sync thread was not reused" );
    // the pooled thread only borrows a player for one call
    auto [ id5, ok5 ] = run( "hook", 0, 77 );
    e.sched.cancelPlayer( 77 );
    auto [ id6, ok6 ] = run( "hook", 0 );
    REQUIRE( ok5 && ok6 && id5 == id1 && id6 == id1, "cancelPlayer dropped the idle pooled sync thread" );
    CHECK( e.expect( *ctx, "count", 3 ) );
    REQUIRE( e.host.errorContains( "boom" ), "no error from bad()" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 1, "expected only the pooled sync thread" );
    ctx->close();
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "the pooled sync thread outlived close" );
    return std::nullopt;
  }

  Fail caseCoroutineIdentity( Env& e )
  {
    auto ctx = e.open( "coroutine_identity.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( e.num( *ctx, "clock" ) == static_cast< double >( e.base ),
             "a host call inside a coroutine created by another (finished) task did not see the running task" );
    return std::nullopt;
  }

  Fail caseArgRange( Env& e )
  {
    auto ctx = e.open( "arg_range.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    CHECK( e.expect( *ctx, "truncated", 255 ) );
    CHECK( e.expect( *ctx, "tooBig", 1 ) );
    CHECK( e.expect( *ctx, "negative", 1 ) );
    CHECK( e.expect( *ctx, "nan", 1 ) );
    return std::nullopt;
  }

  Fail caseNamecall( Env& e )
  {
    auto ctx = e.open( "namecall.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    REQUIRE( !e.num( *ctx, "waited" ), "the Luau method did not yield" );
    e.tick( *ctx, 0 );
    CHECK( e.expect( *ctx, "native", 1 ) );
    CHECK( e.expect( *ctx, "closure", 2 ) );
    CHECK( e.expect( *ctx, "waited", 3 ) );
    CHECK( e.expect( *ctx, "unknown", 1 ) );
    CHECK( e.expect( *ctx, "named", 1 ) );

    // a raw method that yields (as player:eventAction does): the host's resume values are the method's results
    ScriptThread* parked = nullptr;
    e.sched.forEach( [ & ]( ScriptThread& t )
    {
      if( t.ctx == ctx.get() && t.wait == WaitKind::Park )
        parked = &t;
    } );
    REQUIRE( parked, "counter:pause() did not park the task" );
    e.sched.resume( *parked, []( lua_State* co )
    {
      lua_pushnumber( co, 42 );
      return 1;
    } );
    CHECK( e.expect( *ctx, "resumed", 42 ) );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "the task did not finish" );
    return std::nullopt;
  }

  Fail caseRaceEmpty( Env& e )
  {
    auto ctx = e.open( "race_empty.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    CHECK( e.expect( *ctx, "rejected", 1 ) );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "async.race( {} ) left its task parked" );
    return std::nullopt;
  }

  Fail caseErgonomics( Env& e )
  {
    auto ctx = e.open( "ergonomics.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    e.tick( *ctx, 0 );
    e.tick( *ctx, 1 );
    e.tick( *ctx, 2 );
    CHECK( e.expect( *ctx, "exitRan", 0 ) );
    CHECK( e.expect( *ctx, "cancelledSentinel", 1 ) );
    CHECK( e.expect( *ctx, "exitRan2", 0 ) );
    REQUIRE( e.host.errorContains( "task.delay(0) callback exploding", "kaboom" ),
             "the task.delay error does not name the callback" );
    return std::nullopt;
  }

  Fail caseRequire( Env& e )
  {
    auto ctx = e.open( "require.luau" );
    REQUIRE( ctx, "open failed" );
    CHECK( e.expect( *ctx, "same", 1 ) );
    CHECK( e.expect( *ctx, "cycle", 1 ) );
    CHECK( e.expect( *ctx, "outside", 1 ) );
    return std::nullopt;
  }

  Fail caseBind( Env& e )
  {
    auto ctx = e.open( "bind.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->startHandler( "go" );
    CHECK( e.expect( *ctx, "r1", 2 ) );
    CHECK( e.expect( *ctx, "r2", 5 ) );
    CHECK( e.expect( *ctx, "r3", 5 ) );
    auto defs = e.reg.definitions();
    REQUIRE( defs.find( "declare extern type Counter with" ) != std::string::npos,
             "definitions lack 'declare extern type Counter with'" );
    REQUIRE( defs.find( "function add(self, n: number): number" ) != std::string::npos,
             "definitions lack the add signature" );
    auto problems = e.reg.verify( e.rt.state() );
    std::string all;
    for( auto& p : problems )
      all += "\n    " + p;
    REQUIRE( problems.size() == 1 && problems[ 0 ].find( "testlib.missing" ) != std::string::npos,
             "verify() returned:" + all );
    return std::nullopt;
  }

  Fail caseReadOnlyGlobals( Env& e )
  {
    auto locked = e.open( "readonly_globals.luau", true );
    REQUIRE( locked, "open failed" );
    locked->startHandler( "go" );
    REQUIRE( e.host.errorContains( "readonly" ), "no readonly error" );
    REQUIRE( !e.num( *locked, "x" ), "x was written" );
    auto open = e.open( "readonly_globals.luau", false );
    REQUIRE( open, "open failed" );
    open->startHandler( "go" );
    CHECK( e.expect( *open, "x", 1 ) );
    return std::nullopt;
  }

  Fail caseSnapshot( Env& e )
  {
    int ref = LuauSnapshot::newTable( e.rt );
    auto w = std::make_unique< LuauContext >( e.rt, e.sched, ScriptKind::None, 0, e.root / "snapshot_write.luau" );
    REQUIRE( w->open( false ), "open write failed" );
    w->setGlobalFromRef( "store", ref );
    w->startHandler( "go" );

    std::vector< std::string > dropped;
    auto saved = LuauSnapshot::capture( e.rt, ref, dropped );
    REQUIRE( dropped.size() == 1 && dropped[ 0 ] == "fn: function",
             "dropped = " + ( dropped.empty() ? std::string( "{}" ) : dropped[ 0 ] ) );

    int ref2 = LuauSnapshot::restore( e.rt, saved );
    auto r = std::make_unique< LuauContext >( e.rt, e.sched, ScriptKind::None, 0, e.root / "snapshot_read.luau" );
    REQUIRE( r->open( false ), "open read failed" );
    r->setGlobalFromRef( "store", ref2 );
    r->startHandler( "go" );
    CHECK( e.expect( *r, "ok", 1 ) );

    dropped.clear();
    LuauSnapshot::stripCode( e.rt, ref, dropped );
    w->startHandler( "check" );
    CHECK( e.expect( *w, "fnGone", 1 ) );

    LuauSnapshot::release( e.rt, ref );
    LuauSnapshot::release( e.rt, ref2 );
    return std::nullopt;
  }

  Fail caseSnapshotDeep( Env& e )
  {
    int ref = LuauSnapshot::newTable( e.rt );
    auto ctx = e.open( "snapshot_deep.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->setGlobalFromRef( "store", ref );
    ctx->startHandler( "go" );
    CHECK( e.expect( *ctx, "again", 1 ) );
    CHECK( e.expect( *ctx, "loads", 1 ) );

    std::vector< std::string > dropped;
    auto saved = LuauSnapshot::capture( e.rt, ref, dropped );
    REQUIRE( dropped.size() == 1 && dropped[ 0 ].find( "depth > 32" ) != std::string::npos,
             "expected one 'depth > 32' drop, got " + std::to_string( dropped.size() ) );
    LuauSnapshot::stripCode( e.rt, ref, dropped );
    LuauSnapshot::release( e.rt, ref );

    int ref2 = LuauSnapshot::restore( e.rt, saved );
    ctx->setGlobalFromRef( "store", ref2 );
    ctx->startHandler( "check" );
    CHECK( e.expect( *ctx, "levels", 32 ) );
    CHECK( e.expect( *ctx, "alias", 1 ) );
    LuauSnapshot::release( e.rt, ref2 );
    return std::nullopt;
  }

  Fail caseExitHooks( Env& e )
  {
    auto ctx = e.open( "exit_hooks.luau" );
    REQUIRE( ctx, "open failed" );

    ctx->startHandler( "nest" );
    REQUIRE( e.host.errorContains( "nested too deeply" ), "self-registering exit callback was not bounded" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "threads left after nest" );

    ctx->startHandler( "reenter" );
    e.tick( *ctx, 1 );
    CHECK( e.expect( *ctx, "reentered", 1 ) );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "threads left after reenter" );

    ctx->startHandler( "hooks" );
    CHECK( e.expect( *ctx, "hooksDone", 1 ) );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 1, "expected only the hooks thread" );
    size_t hooks = 0;
    uint32_t hooksId = 0;
    e.sched.forEach( [ & ]( ScriptThread& t )
    {
      if( t.ctx != ctx.get() )
        return;
      hooks += t.exitRefs.size();
      hooksId = t.id;
    } );
    REQUIRE( hooks == 0, std::to_string( hooks ) + " exit hooks left on the calling thread" );
    e.sched.cancel( hooksId );

    ctx->startHandler( "waiter" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 3, "expected the waiter and two subtasks" );
    uint32_t waiterId = 0;
    e.sched.forEach( [ & ]( ScriptThread& t )
    {
      if( t.ctx == ctx.get() && waiterId == 0 )
        waiterId = t.id;
    } );
    e.sched.cancel( waiterId );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "subtasks outlived the cancelled waiter" );
    return std::nullopt;
  }

  // a number field of a registry table (one that outlives the contexts writing it)
  std::optional< double > refNumber( LuauRuntime& rt, int ref, const char* name )
  {
    lua_State* L = rt.state();
    lua_getref( L, ref );
    lua_rawgetfield( L, -1, name );
    std::optional< double > out;
    if( lua_isnumber( L, -1 ) )
      out = lua_tonumber( L, -1 );
    lua_pop( L, 2 );
    return out;
  }

  // a temporary directory for fixtures the test edits, removed on scope exit
  struct Scratch
  {
    fs::path dir;
    fs::file_time_type stamp = fs::file_time_type::clock::now();

    explicit Scratch( const std::string& name )
    {
      auto unique = std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() );
      dir = fs::temp_directory_path() / ( "sapphire_luau_selftest_" + name + "_" + unique );
      fs::create_directories( dir );
    }

    ~Scratch()
    {
      std::error_code ec;
      fs::remove_all( dir, ec );
    }

    // every write gets a later mtime: the compile cache and hot reload detect changes by mtime alone
    fs::path write( const std::string& rel, const std::string& text )
    {
      auto file = dir / rel;
      fs::create_directories( file.parent_path() );
      {
        std::ofstream out( file, std::ios::binary | std::ios::trunc );
        out << text;
      }
      stamp += std::chrono::seconds( 2 );
      fs::last_write_time( file, stamp );
      return file;
    }
  };

  Fail caseDrainBudget( Env& e )
  {
    auto ctx = e.open( "drain_budget.luau" );
    REQUIRE( ctx, "open failed" );
    double cap = e.num( *ctx, "CAP" ).value_or( 0 );
    e.rt.setTimeBudgetMs( 30 );
    auto start = std::chrono::steady_clock::now();
    ctx->startHandler( "go" );
    auto ms = std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - start ).count();
    double n1 = e.num( *ctx, "n" ).value_or( 0 );
    e.sched.drain();
    double n2 = e.num( *ctx, "n" ).value_or( 0 );
    e.rt.setTimeBudgetMs( 1000 );
    REQUIRE( n1 > 0, "the ping-pong never ran" );
    REQUIRE( n1 < cap, "the first drain ran the ping-pong to its cap in " + std::to_string( ms ) + " ms" );
    REQUIRE( ms < 1000, "the first drain took " + std::to_string( ms ) + " ms with a 30 ms budget" );
    REQUIRE( n2 > n1, "the next drain did not resume the remaining ready threads" );
    REQUIRE( n2 < cap, "the second drain ran the ping-pong to its cap" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 2, "expected both ping-pong tasks alive" );
    return std::nullopt;
  }

  Fail caseSpawnClosing( Env& e )
  {
    int ref = LuauSnapshot::newTable( e.rt );
    size_t before = e.sched.threadCount();
    auto ctx = e.open( "spawn_closing.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->setGlobalFromRef( "store", ref );
    ctx->startHandler( "go" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 1, "expected one waiting task" );
    ctx->close();
    auto rejected = refNumber( e.rt, ref, "spawnRejected" );
    LuauSnapshot::release( e.rt, ref );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 0, "threads left in the closed context" );
    REQUIRE( e.sched.threadCount() == before, "a task started by an exit callback outlived the closed context" );
    REQUIRE( rejected == 1.0, "task.spawn in an exit callback during close was not rejected" );
    REQUIRE( e.host.errorContains( "being unloaded" ), "task.delay in an exit callback during close raised no error" );
    return std::nullopt;
  }

  Fail caseWaitEdges( Env& e )
  {
    auto ctx = e.open( "wait_edges.luau" );
    REQUIRE( ctx, "open failed" );
    for( auto h : { "nanWait", "negWait", "hugeWait", "overflowWait", "farWait" } )
      REQUIRE( ctx->startHandler( h ), std::string( "no " ) + h + "()" );
    e.tick( *ctx, 0 );
    REQUIRE( e.num( *ctx, "nanDone" ) == 1.0, "task.wait(0/0) did not act as task.wait(0)" );
    REQUIRE( e.num( *ctx, "negDone" ) == 1.0, "task.wait(-1) did not act as task.wait(0)" );
    REQUIRE( !e.num( *ctx, "hugeDone" ), "task.wait(math.huge) woke" );
    REQUIRE( !e.num( *ctx, "overflowDone" ), "a wait past the end of the clock wrapped around and woke" );
    REQUIRE( !e.num( *ctx, "farDone" ), "task.wait(1e9) woke at once" );
    e.tick( *ctx, 1000000000000ull - 1 );
    REQUIRE( !e.num( *ctx, "farDone" ), "task.wait(1e9) woke early" );
    e.tick( *ctx, 1000000000000ull );
    REQUIRE( e.num( *ctx, "farDone" ) == 1.0, "task.wait(1e9) did not wake after 1e9 s" );
    e.sched.updateTimers( ctx->clock(), UINT64_MAX - 1 );
    e.sched.drain();
    REQUIRE( !e.num( *ctx, "hugeDone" ) && !e.num( *ctx, "overflowDone" ), "a never-waking wait woke at the end of time" );
    REQUIRE( e.sched.threadCount( ctx.get() ) == 2, "expected the two never-waking tasks" );
    return std::nullopt;
  }

  Fail caseStripCode( Env& e )
  {
    int ref = LuauSnapshot::newTable( e.rt );
    auto ctx = e.open( "strip_code.luau" );
    REQUIRE( ctx, "open failed" );
    ctx->setGlobalFromRef( "store", ref );
    ctx->startHandler( "go" );
    std::vector< std::string > dropped;
    LuauSnapshot::stripCode( e.rt, ref, dropped );
    ctx->startHandler( "check" );
    LuauSnapshot::release( e.rt, ref );

    std::vector< std::string > want = { "co: thread", "fnKey[function]: function key", "meta: metatable",
                                        "ro.f: function (readonly table, not stripped)",
                                        "roMeta: metatable (readonly table, not stripped)" };
    std::sort( dropped.begin(), dropped.end() );
    std::sort( want.begin(), want.end() );
    std::string got;
    for( auto& d : dropped )
      got += "\n    " + d;
    REQUIRE( dropped == want, "dropped:" + got );
    CHECK( e.expect( *ctx, "fnKeyGone", 1 ) );
    CHECK( e.expect( *ctx, "metaGone", 1 ) );
    CHECK( e.expect( *ctx, "roKept", 1 ) );
    CHECK( e.expect( *ctx, "roMetaKept", 1 ) );
    CHECK( e.expect( *ctx, "coGone", 1 ) );
    CHECK( e.expect( *ctx, "plainKept", 1 ) );
    return std::nullopt;
  }

  Fail caseRequireRetry( Env& )
  {
    // require only loads modules under the runtime's root: the scratch fixtures get a runtime of their own
    Scratch s( "require" );
    StubHost host;
    LuauRuntime rt;
    LuauScheduler sched( rt, host );
    Sapphire::Scripting::Bind::Registry reg;
    Bindings::bindCore( reg );
    Bindings::bindCommon( reg );
    REQUIRE( rt.start( LuauRuntime::defaultScriptRoot() / "common.luau", s.dir, [ & ]( lua_State* L ) { reg.install( L ); } ),
             "the scratch runtime did not start" );
    Env e{ rt, sched, host, reg, s.dir };

    auto mainFile = s.write( "main.luau",
                             "function try()\n"
                             "  local ok, m = pcall(require, \"./mod\")\n"
                             "  loaded = if ok then m.v else nil\n"
                             "  failed = if ok then 0 else 1\n"
                             "  circular = if not ok and string.find(tostring(m), \"circular\", 1, true) then 1 else 0\n"
                             "end\n" );
    auto modFile = s.write( "mod.luau", "error(\"mod broken\")\n" );
    auto ctx = std::make_unique< LuauContext >( rt, sched, ScriptKind::None, 0, mainFile );
    REQUIRE( ctx->open( false ), "open failed: " + ctx->lastError() );

    auto attempt = [ & ]( const char* what, bool wantOk ) -> Fail
    {
      ctx->startHandler( "try" );
      if( e.num( *ctx, "circular" ) == 1.0 )
        return std::string( what ) + ": a failed require left the module marked as loading (circular require)";
      if( e.num( *ctx, "failed" ) != ( wantOk ? 0.0 : 1.0 ) )
        return std::string( what ) + ": require " + ( wantOk ? "failed" : "succeeded" );
      return std::nullopt;
    };
    CHECK( attempt( "runtime error", false ) );
    CHECK( attempt( "retry, unchanged", false ) );
    REQUIRE( ctx->dependencies().count( modFile.lexically_normal().generic_string() ),
             "the failed require was not recorded as a dependency" );
    s.write( "mod.luau", "return { v = \n" );
    CHECK( attempt( "syntax error", false ) );
    s.write( "mod.luau", "return { v = 7 }\n" );
    CHECK( attempt( "fixed", true ) );
    CHECK( e.expect( *ctx, "loaded", 7 ) );
    return std::nullopt;
  }

  Fail caseHandlerRefs( Env& e )
  {
    for( auto& [ kind, list ] : e.reg.handlerSets )
      for( auto& [ name, sig ] : list )
      {
        auto h = scriptHandlerFromName( name.c_str() );
        REQUIRE( h != ScriptHandler::Count, "handler '" + name + "' of " + kind + " scripts is not a ScriptHandler" );
        REQUIRE( name == scriptHandlerName( h ), "scriptHandlerName does not round-trip '" + name + "'" );
      }

    auto ctx = e.open( "handler_refs.luau" );
    REQUIRE( ctx, "open failed" );
    REQUIRE( ctx->handlerRef( ScriptHandler::onTalk ) != LUA_NOREF, "onTalk was not resolved" );
    REQUIRE( ctx->handlerRef( ScriptHandler::onEmote ) == LUA_NOREF, "an undefined handler has a ref" );

    auto fire = [ & ]
    {
      auto* t = e.sched.syncThread( *ctx, 0 );
      lua_getref( t->co, ctx->handlerRef( ScriptHandler::onTalk ) );
      return e.sched.runSync( *t, 0 );
    };
    REQUIRE( fire(), "onTalk failed" );
    CHECK( e.expect( *ctx, "calls", 1 ) );

    // handlers are bound at load: reassigning the global later does not change dispatch
    REQUIRE( ctx->startHandler( "rebind" ), "no rebind()" );
    REQUIRE( fire(), "onTalk failed after rebind" );
    CHECK( e.expect( *ctx, "calls", 2 ) );
    CHECK( e.expect( *ctx, "rebound", 0 ) );

    ctx->close();
    REQUIRE( ctx->handlerRef( ScriptHandler::onTalk ) == LUA_NOREF, "a closed context kept its handler refs" );
    return std::nullopt;
  }

  //////////////////////////////////////////////////////////////////////////////////////////// LuauScriptMgr

  class TestMgr : public LuauScriptMgr
  {
  public:
    using LuauScriptMgr::onYield;

    // runs once, inside the next script yield (the scheduler is not idle there)
    std::function< void( ScriptThread& ) > yieldHook;
    std::vector< std::string > errors;

    void onYield( ScriptThread& thread ) override
    {
      LuauScriptMgr::onYield( thread );
      if( auto hook = std::exchange( yieldHook, nullptr ) )
        hook( thread );
    }

    void reportError( ScriptThread& thread, const std::string& message ) override
    {
      errors.push_back( message );
      LuauScriptMgr::reportError( thread, message );
    }

    bool errorContains( const std::string& text ) const
    {
      return std::any_of( errors.begin(), errors.end(), [ & ]( const std::string& e ) { return e.find( text ) != std::string::npos; } );
    }
  };

  constexpr uint32_t kHot = 0x00A10001;
  constexpr uint32_t kLate = 0x00A10002;

  const char* const kHotScript = R"(script.event(0x00A10001)
local util = require("./lib/util")
VERSION = util.version
function onEmote() end
function hold() task.wait(math.huge) end
function act()
  while true do
    task.wait(math.huge)
    resumes = (resumes or 0) + 1
  end
end
)";

  const char* const kLateScript = R"(script.event(0x00A10002)
local bad = require("./lib/bad")
VALUE = bad.value
function onEmote() end
)";

  // a manager on a scratch scripts directory: events/hot.luau requires events/lib/util.luau, events/late.luau requires
  // events/lib/bad.luau
  struct MgrFixture
  {
    Scratch files{ "mgr" };
    std::unique_ptr< TestMgr > mgr;
    uint64_t now = 0;

    bool start( const std::string& util, const std::string& bad )
    {
      fs::copy_file( LuauRuntime::defaultScriptRoot() / "common.luau", files.dir / "common.luau" );
      files.write( "events/hot.luau", kHotScript );
      files.write( "events/late.luau", kLateScript );
      files.write( "events/lib/util.luau", util );
      files.write( "events/lib/bad.luau", bad );
      Sapphire::Common::Config::WorldConfig::Scripts cfg{};
      cfg.luauPath = files.dir.string();
      cfg.hotSwap = true;
      cfg.luauTimeBudgetMs = 1000;
      mgr = std::make_unique< TestMgr >();
      mgr->init( cfg, {} );
      now = Sapphire::Common::Util::getTimeMs();
      tick(); // the first poll records the file times
      return mgr->running();
    }

    void tick()
    {
      now += 600;
      mgr->update( now );
    }

    // hot reload acts on a changed file once its mtime has been stable for 200 ms: two polls
    void settle()
    {
      tick();
      tick();
    }

    // the live singleton context of an event script
    LuauContext* singleton( uint32_t id )
    {
      // a sync call leaves the context's pooled sync thread behind, which leads to the context
      auto* script = mgr->script( ScriptKind::Event, id );
      if( !script || !mgr->syncHook( *script, ScriptHandler::onEmote, {} ) )
        return nullptr;
      LuauContext* found = nullptr;
      mgr->scheduler().forEach( [ & ]( ScriptThread& t )
      {
        if( t.ctx && t.ctx->isOpen() && t.ctx->kind() == ScriptKind::Event && t.ctx->scriptId() == id )
          found = t.ctx;
      } );
      return found;
    }

    std::optional< double > num( uint32_t id, const char* name )
    {
      double v = 0;
      auto* ctx = singleton( id );
      if( ctx && ctx->getGlobalNumber( name, v ) )
        return v;
      return std::nullopt;
    }

    // starts handler as a task of the singleton; its thread id while it lives, else 0
    uint32_t startTask( uint32_t id, const char* handler )
    {
      auto* ctx = singleton( id );
      if( !ctx )
        return 0;
      std::vector< uint32_t > before;
      mgr->scheduler().forEach( [ & ]( ScriptThread& t ) { before.push_back( t.id ); } );
      ctx->startHandler( handler );
      uint32_t started = 0;
      mgr->scheduler().forEach( [ & ]( ScriptThread& t )
      {
        if( t.ctx == ctx && std::find( before.begin(), before.end(), t.id ) == before.end() )
          started = t.id;
      } );
      return started;
    }

    bool alive( uint32_t threadId ) { return mgr->scheduler().find( threadId ) != nullptr; }
  };

  Fail caseMgrBrokenLib( Env& )
  {
    MgrFixture f;
    REQUIRE( f.start( "return { version = 1 }\n", "return { value = 1 }\n" ), "the manager did not start" );
    REQUIRE( f.num( kHot, "VERSION" ) == 1.0, "hot.luau did not load" );
    uint32_t hold = f.startTask( kHot, "hold" );
    REQUIRE( hold != 0, "hold() did not start" );

    f.files.write( "events/lib/util.luau", "return { version = \n" );
    f.settle();
    REQUIRE( f.mgr->errorContains( "lib/util" ), "the lib change did not trigger a reload attempt of its dependent" );
    REQUIRE( f.alive( hold ), "a reload against a broken lib replaced the running version" );
    REQUIRE( f.num( kHot, "VERSION" ) == 1.0, "the running version changed" );

    f.files.write( "events/lib/util.luau", "return { version = 2 }\n" );
    f.settle();
    REQUIRE( !f.alive( hold ), "fixing the lib did not reload its dependent" );
    REQUIRE( f.num( kHot, "VERSION" ) == 2.0, "the reloaded script did not see the fixed lib" );
    return std::nullopt;
  }

  Fail caseMgrFailedRetry( Env& )
  {
    MgrFixture f;
    REQUIRE( f.start( "return { version = 1 }\n", "error(\"bad lib\")\n" ), "the manager did not start" );
    REQUIRE( f.mgr->script( ScriptKind::Event, kHot ), "hot.luau is not registered" );
    REQUIRE( !f.mgr->script( ScriptKind::Event, kLate ), "late.luau registered despite its broken lib" );

    // the lib is not a script and late.luau has no entry: only the failed-script list leads back to it
    f.files.write( "events/lib/bad.luau", "return { value = 5 }\n" );
    f.settle();
    REQUIRE( f.mgr->script( ScriptKind::Event, kLate ), "late.luau was not retried when its lib was fixed" );
    REQUIRE( f.num( kLate, "VALUE" ) == 5.0, "late.luau did not see the fixed lib" );
    return std::nullopt;
  }

  Fail caseMgrActionTokens( Env& )
  {
    MgrFixture f;
    REQUIRE( f.start( "return { version = 1 }\n", "return { value = 1 }\n" ), "the manager did not start" );
    uint32_t id = f.startTask( kHot, "act" );
    REQUIRE( id != 0, "act() did not start" );

    // what player:eventAction does before yielding (it needs a player, an open event and EventAction data)
    auto arm = [ & ]() -> uint64_t
    {
      auto* t = f.mgr->scheduler().find( id );
      if( !t )
        return 0;
      t->wait = WaitKind::Action;
      return f.mgr->beginActionWait( id );
    };
    auto resumes = [ & ]() { return f.num( kHot, "resumes" ).value_or( 0 ); };

    uint64_t stale = arm();
    uint64_t current = arm(); // a newer wait on the same thread supersedes stale
    f.mgr->deferActionResume( id, stale, true );
    f.tick();
    REQUIRE( resumes() == 0, "a superseded action token resumed the thread" );
    f.mgr->deferActionResume( id, current, true );
    f.tick();
    REQUIRE( resumes() == 1, "the current action token did not resume the thread" );

    uint64_t next = arm();
    f.mgr->deferActionResume( id, current, false ); // a late callback of the finished wait
    f.tick();
    REQUIRE( resumes() == 1, "a consumed action token resumed the thread's next wait" );
    f.mgr->deferActionResume( id, next, true );
    f.tick();
    REQUIRE( resumes() == 2, "the next action token did not resume the thread" );
    return std::nullopt;
  }

  Fail caseMgrDeferred( Env& )
  {
    MgrFixture f;
    REQUIRE( f.start( "return { version = 1 }\n", "return { value = 1 }\n" ), "the manager did not start" );

    f.mgr->yieldHook = [ & ]( ScriptThread& ) { f.mgr->restartVm(); };
    uint32_t hold = f.startTask( kHot, "hold" );
    REQUIRE( hold != 0, "restartVm inside a script segment was not deferred (the task is gone)" );
    f.tick();
    REQUIRE( !f.alive( hold ), "the deferred VM restart did not run on the next update" );
    REQUIRE( f.num( kHot, "VERSION" ) == 1.0, "hot.luau is missing after the restart" );

    f.mgr->yieldHook = [ & ]( ScriptThread& ) { f.mgr->reloadByName( "hot.luau" ); };
    uint32_t hold2 = f.startTask( kHot, "hold" );
    REQUIRE( hold2 != 0, "a reload inside a script segment was not deferred (the task is gone)" );
    f.tick();
    REQUIRE( !f.alive( hold2 ), "the deferred reload did not run on the next update" );
    REQUIRE( f.num( kHot, "VERSION" ) == 1.0, "hot.luau is missing after the reload" );
    return std::nullopt;
  }
}

int main( int argc, char** argv )
{
  std::setvbuf( stdout, nullptr, _IONBF, 0 );
  fs::path cases = argc > 1 ? fs::path( argv[ 1 ] ) : fs::path( "../../src/tools/luau_selftest/cases" );
  cases = fs::absolute( cases );
  if( !fs::is_directory( cases ) )
  {
    std::fprintf( stderr, "cases directory not found: %s\n", cases.string().c_str() );
    return 1;
  }

  StubHost host;
  LuauRuntime rt;
  LuauScheduler sched( rt, host );
  Sapphire::Scripting::Bind::Registry reg;
  Bindings::bindCore( reg );
  Bindings::bindCommon( reg );
  Sapphire::Scripting::Bind::Class< CounterObj >( reg, "Counter", Bindings::HandleTag::Counter )
    .prop( "value", []( CounterObj& c ) { return c.value; }, []( CounterObj& c, double v ) { c.value = v; } )
    .method( "add", []( CounterObj& c, double n ) { c.value += n; return c.value; }, "n" )
    .method( "mask", []( CounterObj&, Sapphire::Scripting::Bind::Flags f ) { return f.value; }, "flags" )
    .method( "byte", []( CounterObj&, uint8_t b ) { return b; }, "value" )
    .rawMethod( "pause", []( lua_State* L ) -> int
    {
      Bindings::beginYield( L ).wait = WaitKind::Park;
      return lua_yield( L, 0 );
    }, "(self) -> number" )
    .luauMethod( "waitAdd", "(self, n: number) -> number" );
  Sapphire::Scripting::Bind::Library( reg, "testlib" ).luauFunction( "missing", "() -> ()" );
  reg.global( "counter", "Counter", []( lua_State* L )
  {
    Sapphire::Scripting::Bind::Class< CounterObj >::push( L, Bindings::HandleTag::Counter, CounterObj{} );
  } );

  // a Counter method implemented in Luau, as common.luau implements Player.playScene
  static const char kCounterLuau[] = "function Counter.waitAdd(self, n) task.wait(0) return self:add(n) end";
  auto registerBindings = [ & ]( lua_State* L )
  {
    reg.install( L );
    size_t size = 0;
    char* bytecode = luau_compile( kCounterLuau, sizeof( kCounterLuau ) - 1, nullptr, &size );
    int status = luau_load( L, "=selftest", bytecode, size, 0 );
    std::free( bytecode );
    if( status == 0 )
      status = lua_pcall( L, 0, 0, 0 );
    if( status != 0 )
    {
      std::fprintf( stderr, "Counter.waitAdd failed to load: %s\n", lua_tostring( L, -1 ) );
      lua_pop( L, 1 );
    }
  };

  if( !rt.start( LuauRuntime::defaultScriptRoot() / "common.luau", cases, registerBindings ) )
  {
    std::fprintf( stderr, "runtime failed to start (common.luau at %s)\n",
                  ( LuauRuntime::defaultScriptRoot() / "common.luau" ).string().c_str() );
    return 1;
  }

  Env env{ rt, sched, host, reg, cases };

  // output stays PASS/FAIL lines: the log goes to the buffer sink only and is printed for failing cases
  auto logBuffer = Sapphire::Logger::getBufferSink();
  if( auto logger = spdlog::get( "logger" ) )
  {
    auto& sinks = logger->sinks();
    sinks.erase( std::remove_if( sinks.begin(), sinks.end(), [ & ]( const spdlog::sink_ptr& s ) { return s != logBuffer; } ),
                 sinks.end() );
  }

  struct Case
  {
    const char* name;
    Fail ( *fn )( Env& );
  };
  const Case all[] = {
    { "timer", caseTimer },
    { "spawn_await", caseSpawnAwait },
    { "task_cancel", caseTaskCancel },
    { "change_wait", caseChangeWait },
    { "change_poll", caseChangePoll },
    { "change_data", caseChangeData },
    { "error_isolated", caseErrorIsolated },
    { "budget", caseBudget },
    { "rebuild", caseRebuild },
    { "sandbox", caseSandbox },
    { "coroutine_guard", caseCoroutineGuard },
    { "spawn_toplevel", caseSpawnTopLevel },
    { "require", caseRequire },
    { "bind", caseBind },
    { "readonly_globals", caseReadOnlyGlobals },
    { "snapshot", caseSnapshot },
    { "snapshot_deep", caseSnapshotDeep },
    { "exit_hooks", caseExitHooks },
    { "sync_reuse", caseSyncReuse },
    { "ergonomics", caseErgonomics },
    { "handler_refs", caseHandlerRefs },
    { "drain_budget", caseDrainBudget },
    { "spawn_closing", caseSpawnClosing },
    { "wait_edges", caseWaitEdges },
    { "strip_code", caseStripCode },
    { "require_retry", caseRequireRetry },
    { "mgr_broken_lib", caseMgrBrokenLib },
    { "mgr_failed_retry", caseMgrFailedRetry },
    { "mgr_action_tokens", caseMgrActionTokens },
    { "mgr_deferred", caseMgrDeferred },
    { "coroutine_identity", caseCoroutineIdentity },
    { "arg_range", caseArgRange },
    { "namecall", caseNamecall },
    { "race_empty", caseRaceEmpty },
  };

  int passed = 0;
  int total = 0;
  for( auto& c : all )
  {
    ++total;
    host.errors.clear();
    logBuffer->clearMessages();
    Fail f = c.fn( env );
    if( f )
    {
      std::printf( "FAIL %s: %s\n", c.name, f->c_str() );
      for( auto& line : logBuffer->getMessages() )
        std::printf( "  log: %s\n", line.c_str() );
    }
    else
    {
      std::printf( "PASS %s\n", c.name );
      ++passed;
    }
  }
  std::printf( "%d/%d passed\n", passed, total );

  rt.stop();
  return passed == total ? 0 : 1;
}
