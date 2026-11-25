#include "LuauContext.h"
#include "LuauRuntime.h"
#include "LuauScheduler.h"

#include <lua.h>
#include <lualib.h>

#include <cstring>

namespace Sapphire::Scripting
{
  const char* scriptKindName( ScriptKind kind )
  {
    switch( kind )
    {
      case ScriptKind::None:
        return "none";
      case ScriptKind::Instance:
        return "instance";
      case ScriptKind::Quest:
        return "quest";
      case ScriptKind::Event:
        return "event";
      case ScriptKind::QuestBattle:
        return "questbattle";
      case ScriptKind::Action:
        return "action";
      case ScriptKind::Status:
        return "status";
      case ScriptKind::Zone:
        return "zone";
      case ScriptKind::EObj:
        return "eobj";
      case ScriptKind::Count:
        break;
    }
    return "unknown";
  }

  uint32_t normaliseScriptId( ScriptKind kind, uint32_t declaredId )
  {
    switch( kind )
    {
      case ScriptKind::Instance:
        return ( 0x8003u << 16 ) | ( declaredId & 0xFFFF );
      case ScriptKind::QuestBattle:
        return ( 0x8006u << 16 ) | ( declaredId & 0xFFFF );
      case ScriptKind::Quest:
        return ( 1u << 16 ) | ( declaredId & 0xFFFF );
      default:
        return declaredId;
    }
  }

  static const char* const kHandlerNames[] = {
#define X( name ) #name,
    SAPPHIRE_LUAU_HANDLERS( X )
#undef X
  };
  static_assert( std::size( kHandlerNames ) == static_cast< size_t >( ScriptHandler::Count ) );

  const char* scriptHandlerName( ScriptHandler handler )
  {
    auto i = static_cast< size_t >( handler );
    return i < std::size( kHandlerNames ) ? kHandlerNames[ i ] : "unknown";
  }

  ScriptHandler scriptHandlerFromName( const char* name )
  {
    for( size_t i = 0; i < std::size( kHandlerNames ); ++i )
      if( std::strcmp( kHandlerNames[ i ], name ) == 0 )
        return static_cast< ScriptHandler >( i );
    return ScriptHandler::Count;
  }

  LuauContext::LuauContext( LuauRuntime& runtime, LuauScheduler& scheduler, ScriptKind kind, uint32_t scriptId,
                            std::filesystem::path scriptPath ) :
    m_runtime( runtime ),
    m_scheduler( scheduler ),
    m_kind( kind ),
    m_scriptId( scriptId ),
    m_path( std::move( scriptPath ) )
  {
    m_relPath = m_runtime.relativePath( m_path );
    m_handlerRefs.fill( LUA_NOREF );
  }

  LuauContext::~LuauContext()
  {
    close();
  }

  bool LuauContext::open( bool discovery )
  {
    close();
    m_lastError.clear();
    m_discovery = discovery;
    m_declaredKind = ScriptKind::None;
    m_declaredId = 0;

    lua_State* L = m_runtime.state();
    if( !L )
    {
      m_lastError = "the Luau VM is not running";
      return false;
    }

    const auto& chunk = m_runtime.compile( m_path );
    if( !chunk.error.empty() )
    {
      m_lastError = chunk.error;
      return false;
    }

    m_env = lua_newthread( L );
    m_envRef = lua_ref( L, -1 );
    lua_pop( L, 1 );
    luaL_sandboxthread( m_env );
    // several chunks (script + modules) are loaded into one env
    lua_setsafeenv( m_env, LUA_GLOBALSINDEX, false );

    auto* t = m_scheduler.create( *this, 0 );
    t->syncError = "top-level script code must not yield";
    auto name = m_runtime.chunkName( m_path );
    if( luau_load( t->co, name.c_str(), chunk.bytecode.data(), chunk.bytecode.size(), 0 ) != 0 )
    {
      m_lastError = lua_tostring( t->co, -1 ) ? lua_tostring( t->co, -1 ) : "load failed";
      lua_settop( t->co, 0 );
      m_scheduler.cancel( t->id );
      return false;
    }

    // runtime errors of the top-level code are reported through ISchedulerHost::reportError
    if( !m_scheduler.runSync( *t, 0 ) )
      return false;

    resolveHandlers();

    if( !m_discovery && policy.readOnlyGlobals )
      freezeGlobals();
    return true;
  }

  void LuauContext::resolveHandlers()
  {
    if( !m_env )
      return;
    for( size_t i = 0; i < m_handlerRefs.size(); ++i )
    {
      lua_rawgetfield( m_env, LUA_GLOBALSINDEX, kHandlerNames[ i ] );
      if( lua_isfunction( m_env, -1 ) )
        m_handlerRefs[ i ] = lua_ref( m_env, -1 );
      lua_pop( m_env, 1 );
    }
  }

  void LuauContext::freezeGlobals()
  {
    if( m_env )
      lua_setreadonly( m_env, LUA_GLOBALSINDEX, true );
  }

  void LuauContext::close()
  {
    if( !m_env )
      return;

    m_closing = true;
    m_scheduler.cancelAll( *this );
    m_closing = false;

    lua_State* L = m_runtime.state();
    if( L )
    {
      for( auto& [ name, ref ] : m_talkHandlers )
        lua_unref( L, ref );
      for( auto& [ key, ref ] : m_modules )
        lua_unref( L, ref );
      lua_unref( L, m_envRef );
      for( int ref : m_handlerRefs )
        if( ref != LUA_NOREF )
          lua_unref( L, ref );
    }
    m_talkHandlers.clear();
    m_modules.clear();
    m_handlerRefs.fill( LUA_NOREF );
    m_loading.clear();
    m_env = nullptr;
    m_envRef = -1;
  }

  bool LuauContext::isOpen() const
  {
    return m_env != nullptr;
  }

  bool LuauContext::isClosing() const
  {
    return m_closing;
  }

  bool LuauContext::discovery() const
  {
    return m_discovery;
  }

  int LuauContext::handlerRef( ScriptHandler handler ) const
  {
    return m_handlerRefs[ static_cast< size_t >( handler ) ];
  }

  bool LuauContext::pushGlobalFunction( lua_State* L, const char* name ) const
  {
    if( !m_env )
      return false;
    lua_rawgetfield( m_env, LUA_GLOBALSINDEX, name );
    if( !lua_isfunction( m_env, -1 ) )
    {
      lua_pop( m_env, 1 );
      return false;
    }
    lua_xmove( m_env, L, 1 );
    return true;
  }

  ScriptKind LuauContext::kind() const
  {
    return m_kind;
  }

  uint32_t LuauContext::scriptId() const
  {
    return m_scriptId;
  }

  void LuauContext::setIdentity( ScriptKind kind, uint32_t scriptId )
  {
    m_kind = kind;
    m_scriptId = scriptId;
    m_discovery = false;
  }

  ScriptKind LuauContext::declaredKind() const
  {
    return m_declaredKind;
  }

  uint32_t LuauContext::declaredId() const
  {
    return m_declaredId;
  }

  void LuauContext::declare( ScriptKind kind, uint32_t id )
  {
    m_declaredKind = kind;
    m_declaredId = id;
  }

  const std::filesystem::path& LuauContext::path() const
  {
    return m_path;
  }

  const std::string& LuauContext::relPath() const
  {
    return m_relPath;
  }

  const std::set< std::string >& LuauContext::dependencies() const
  {
    return m_dependencies;
  }

  const std::string& LuauContext::lastError() const
  {
    return m_lastError;
  }

  lua_State* LuauContext::envThread() const
  {
    return m_env;
  }

  LuauRuntime& LuauContext::runtime() const
  {
    return m_runtime;
  }

  LuauScheduler& LuauContext::scheduler() const
  {
    return m_scheduler;
  }

  void LuauContext::addTalkHandler( const std::string& eobjName, int ref )
  {
    auto it = m_talkHandlers.find( eobjName );
    if( it != m_talkHandlers.end() )
    {
      lua_unref( m_runtime.state(), it->second );
      it->second = ref;
    }
    else
      m_talkHandlers.emplace( eobjName, ref );
  }

  int LuauContext::talkHandler( const std::string& eobjName ) const
  {
    auto it = m_talkHandlers.find( eobjName );
    return it == m_talkHandlers.end() ? -1 : it->second;
  }

  const std::map< std::string, int >& LuauContext::talkHandlers() const
  {
    return m_talkHandlers;
  }

  int LuauContext::moduleRef( const std::string& key ) const
  {
    auto it = m_modules.find( key );
    return it == m_modules.end() ? LUA_NOREF : it->second;
  }

  void LuauContext::setModuleRef( const std::string& key, int ref )
  {
    m_modules[ key ] = ref;
  }

  bool LuauContext::beginLoading( const std::string& key )
  {
    return m_loading.insert( key ).second;
  }

  void LuauContext::endLoading( const std::string& key )
  {
    m_loading.erase( key );
  }

  void LuauContext::addDependency( const std::string& absolutePath )
  {
    m_dependencies.insert( absolutePath );
  }

  ScriptClock& LuauContext::clock()
  {
    return *m_clock;
  }

  uint64_t LuauContext::clockMs() const
  {
    return m_clock->nowMs;
  }

  bool LuauContext::startHandler( const char* name )
  {
    if( !pushGlobalFunction( m_env, name ) )
      return false;
    auto* t = m_scheduler.create( *this, 0 );
    lua_xmove( m_env, t->co, 1 );
    m_scheduler.start( *t, 0 );
    return true;
  }

  void LuauContext::setGlobalFromRef( const char* name, int ref )
  {
    if( !m_env )
      return;
    lua_getref( m_env, ref );
    lua_rawsetfield( m_env, LUA_GLOBALSINDEX, name );
  }

  bool LuauContext::getGlobalNumber( const char* name, double& out ) const
  {
    if( !m_env )
      return false;
    lua_rawgetfield( m_env, LUA_GLOBALSINDEX, name );
    bool ok = lua_isnumber( m_env, -1 ) != 0;
    if( ok )
      out = lua_tonumber( m_env, -1 );
    lua_pop( m_env, 1 );
    return ok;
  }

  void LuauContext::setGlobalBool( const char* name, bool value )
  {
    if( !m_env )
      return;
    lua_pushboolean( m_env, value ? 1 : 0 );
    lua_rawsetfield( m_env, LUA_GLOBALSINDEX, name );
  }
}
