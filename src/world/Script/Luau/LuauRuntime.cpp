#include "LuauRuntime.h"
#include "LuauScheduler.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <Luau/Common.h>
#include <Luau/ExperimentalFlags.h>

#include <Logging/Logger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
  int64_t steadyMs()
  {
    return std::chrono::duration_cast< std::chrono::milliseconds >(
             std::chrono::steady_clock::now().time_since_epoch() ).count();
  }

  void enableLuauFlags()
  {
    static bool done = false;
    if( done )
      return;
    done = true;
    for( Luau::FValue< bool >* flag = Luau::FValue< bool >::list; flag; flag = flag->next )
      if( std::strncmp( flag->name, "Luau", 4 ) == 0 && !Luau::isAnalysisFlagExperimental( flag->name ) )
        flag->value = true;
  }

  bool readFile( const fs::path& file, std::string& out )
  {
    std::ifstream in( file, std::ios::binary );
    if( !in )
      return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
  }

  // coroutine.resume / coroutine.close; upvalue 1 = the original function, 2 = its name.
  // coroutine.running() inside a task returns the scheduler's thread: resuming or closing it from script code would run
  // it outside the scheduler (or reset it under the scheduler), so both reject task threads. coroutine.wrap needs no
  // guard: it only ever resumes the coroutine it created itself.
  int guardedCoroutine( lua_State* L )
  {
    if( lua_type( L, 1 ) == LUA_TTHREAD )
    {
      auto* rt = Sapphire::Scripting::LuauRuntime::from( L );
      if( rt && rt->scheduler() && rt->scheduler()->isTaskThread( lua_tothread( L, 1 ) ) )
        luaL_error( L, "coroutine.%s: the thread is a task thread (coroutine.running() inside a task); use the task "
                       "library to control tasks", lua_tostring( L, lua_upvalueindex( 2 ) ) );
    }
    lua_pushvalue( L, lua_upvalueindex( 1 ) );
    lua_insert( L, 1 );
    lua_call( L, lua_gettop( L ) - 1, LUA_MULTRET );
    return lua_gettop( L );
  }
}

namespace Sapphire::Scripting
{
  LuauRuntime::~LuauRuntime()
  {
    stop();
  }

  LuauRuntime* LuauRuntime::from( lua_State* L )
  {
    return static_cast< LuauRuntime* >( lua_callbacks( L )->userdata );
  }

  void LuauRuntime::onInterrupt( lua_State* L, int gc )
  {
    if( gc >= 0 )
      return;
    auto* rt = from( L );
    if( !rt || rt->m_budgetMs == 0 || rt->m_deadline == 0 )
      return;
    // interrupts fire on every loop back-edge and call: read the clock on every 64th only
    if( ( ++rt->m_interrupts & 63 ) != 0 )
      return;
    if( steadyMs() > rt->m_deadline )
      luaL_error( L, "script exceeded %d ms time budget", static_cast< int >( rt->m_budgetMs ) );
  }

  bool LuauRuntime::start( const fs::path& preludeFile, const fs::path& moduleRoot, RegisterFn registerBindings )
  {
    stop();
    enableLuauFlags();

    // canonical, so that inRoot can compare canonical module paths against it
    std::error_code rootEc;
    m_root = fs::weakly_canonical( moduleRoot, rootEc );
    if( rootEc )
      m_root = moduleRoot;
    m_state = luaL_newstate();
    lua_State* L = m_state;
    luaL_openlibs( L );

    auto* cb = lua_callbacks( L );
    cb->userdata = this;
    cb->interrupt = &LuauRuntime::onInterrupt;

    // the host kills threads with lua_resetthread, which silently drops finalizers
    lua_getglobal( L, "coroutine" );
    lua_pushnil( L );
    lua_setfield( L, -2, "finally" );
    lua_pop( L, 1 );

    lua_getglobal( L, "coroutine" );
    for( const char* name : { "resume", "close" } )
    {
      lua_getfield( L, -1, name );
      lua_pushstring( L, name );
      lua_pushcclosurek( L, &guardedCoroutine, name, 2, nullptr );
      lua_setfield( L, -2, name );
    }
    lua_pop( L, 1 );

    lua_newtable( L );
    lua_setglobal( L, "__host" );
    lua_newtable( L );
    lua_setglobal( L, "Player" );

    if( registerBindings )
      registerBindings( L );

    std::string source;
    if( !readFile( preludeFile, source ) )
    {
      Logger::error( "[Luau] common.luau failed: cannot read {}", preludeFile.string() );
      stop();
      return false;
    }

    lua_CompileOptions opts{};
    opts.optimizationLevel = 1;
    opts.debugLevel = 1;
    size_t size = 0;
    char* data = luau_compile( source.data(), source.size(), &opts, &size );
    int status = luau_load( L, "@common.luau", data, size, 0 );
    std::free( data );
    if( status == 0 )
      status = lua_pcall( L, 0, 0, 0 );
    if( status != 0 )
    {
      const char* msg = lua_tostring( L, -1 );
      Logger::error( "[Luau] common.luau failed: {}", msg ? msg : "unknown error" );
      stop();
      return false;
    }

    lua_pushnil( L );
    lua_setglobal( L, "__host" );

    luaL_sandbox( L );
    return true;
  }

  void LuauRuntime::stop()
  {
    if( m_state )
    {
      lua_close( m_state );
      m_state = nullptr;
    }
    m_deadline = 0;
  }

  bool LuauRuntime::running() const
  {
    return m_state != nullptr;
  }

  lua_State* LuauRuntime::state() const
  {
    return m_state;
  }

  const fs::path& LuauRuntime::root() const
  {
    return m_root;
  }

  bool LuauRuntime::inRoot( const fs::path& canonicalFile ) const
  {
    auto [ r, f ] = std::mismatch( m_root.begin(), m_root.end(), canonicalFile.begin(), canonicalFile.end() );
    // a root spelled with a trailing separator ends in an empty element
    return ( r == m_root.end() || r->empty() ) && f != canonicalFile.end();
  }

  const CompiledChunk& LuauRuntime::compile( const fs::path& file )
  {
    auto key = file.lexically_normal().generic_string();
    auto& chunk = m_chunks[ key ];

    std::error_code ec;
    auto mtime = fs::last_write_time( file, ec );
    if( ec )
    {
      chunk = CompiledChunk{};
      chunk.error = "cannot read file";
      return chunk;
    }
    if( ( !chunk.bytecode.empty() || !chunk.error.empty() ) && chunk.mtime == mtime )
      return chunk;

    chunk = CompiledChunk{};
    chunk.mtime = mtime;
    std::string source;
    if( !readFile( file, source ) )
    {
      chunk.error = "cannot read file";
      return chunk;
    }

    lua_CompileOptions opts{};
    opts.optimizationLevel = 1;
    opts.debugLevel = 1;
    size_t size = 0;
    char* data = luau_compile( source.data(), source.size(), &opts, &size );
    if( size > 0 && data[ 0 ] == 0 )
      chunk.error = std::string( data + 1, size - 1 );
    else
      chunk.bytecode.assign( data, size );
    std::free( data );
    return chunk;
  }

  std::string LuauRuntime::relativePath( const fs::path& file ) const
  {
    std::error_code ec;
    auto rel = fs::relative( file, m_root, ec );
    if( ec || rel.empty() || rel.native()[ 0 ] == '.' )
      return file.generic_string();
    return rel.generic_string();
  }

  std::string LuauRuntime::chunkName( const fs::path& file ) const
  {
    return "@" + relativePath( file );
  }

  fs::path LuauRuntime::defaultScriptRoot()
  {
#ifdef SAPPHIRE_LUAU_SOURCE_DIR
    return fs::path( SAPPHIRE_LUAU_SOURCE_DIR );
#else
    return fs::path( "./scripts/" );
#endif
  }

  void LuauRuntime::setTimeBudgetMs( uint32_t ms )
  {
    m_budgetMs = ms;
  }

  uint32_t LuauRuntime::timeBudgetMs() const
  {
    return m_budgetMs;
  }

  void LuauRuntime::armDeadline()
  {
    m_deadline = m_budgetMs == 0 ? 0 : steadyMs() + m_budgetMs;
    m_interrupts = 0;
  }

  void LuauRuntime::disarmDeadline()
  {
    m_deadline = 0;
  }

  void LuauRuntime::setScheduler( LuauScheduler* scheduler )
  {
    m_scheduler = scheduler;
  }

  LuauScheduler* LuauRuntime::scheduler() const
  {
    return m_scheduler;
  }
}
