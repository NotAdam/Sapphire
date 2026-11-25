#include "LuauScriptMgr.h"
#include "Bindings/Bindings.h"
#include "Bindings/Handles.h"

#include <lua.h>
#include <lualib.h>

#include <Logging/Logger.h>
#include <Service.h>
#include <Util/Util.h>

#include "Actor/BNpc.h"
#include "Actor/Chara.h"
#include "Actor/EventObject.h"
#include "Actor/Player.h"
#include "Encounter/Encounter.h"
#include "Event/EventHandler.h"
#include "Manager/EventMgr.h"
#include "Manager/PlayerMgr.h"
#include "Territory/InstanceContent.h"
#include "Territory/QuestBattle.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Sapphire::Scripting
{
  namespace
  {
    LuauScriptMgr* s_instance = nullptr;

    const char* const kScriptDirs[] = { "instances", "quests", "events", "questbattles", "actions", "statuses", "zones",
                                        "eobjs" };

    bool perInstance( ScriptKind kind )
    {
      return kind == ScriptKind::Instance || kind == ScriptKind::QuestBattle;
    }

    ScriptEncounterState encounterState( EncounterStatus status )
    {
      switch( status )
      {
        case EncounterStatus::ACTIVE:
          return ScriptEncounterState::Active;
        case EncounterStatus::SUCCESS:
          return ScriptEncounterState::Success;
        default:
          return ScriptEncounterState::Idle;
      }
    }

    std::string firstLine( const std::string& s )
    {
      auto nl = s.find( '\n' );
      return nl == std::string::npos ? s : s.substr( 0, nl );
    }

    std::string toLower( std::string s )
    {
      std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return static_cast< char >( std::tolower( c ) ); } );
      return s;
    }

    std::string trim( const std::string& s )
    {
      size_t b = s.find_first_not_of( " \t\r\n" );
      size_t e = s.find_last_not_of( " \t\r\n" );
      return b == std::string::npos ? std::string() : s.substr( b, e - b + 1 );
    }

    // lookup key of a script file: generic, lexically normalised. Paths from iterating the (canonical) root already
    // have their on-disk spelling, so no filesystem access is needed; require canonicalises its keys to the same form
    std::string pathKey( const fs::path& p )
    {
      return p.lexically_normal().generic_string();
    }

    World::Manager::EventMgr& eventMgr()
    {
      return Common::Service< World::Manager::EventMgr >::ref();
    }

    bool readText( const fs::path& file, std::string& out )
    {
      std::ifstream in( file, std::ios::binary );
      if( !in )
        return false;
      std::ostringstream ss;
      ss << in.rdbuf();
      out = ss.str();
      return true;
    }

    bool writeText( const fs::path& file, const std::string& text )
    {
      std::error_code ec;
      fs::create_directories( file.parent_path(), ec );
      std::ofstream out( file, std::ios::binary | std::ios::trunc );
      if( !out )
        return false;
      out << text;
      return static_cast< bool >( out );
    }

    std::string hexId( uint32_t id )
    {
      char buf[ 16 ];
      std::snprintf( buf, sizeof( buf ), "%08X", id );
      return buf;
    }
  }

  const char* scriptEncounterStateName( ScriptEncounterState state )
  {
    switch( state )
    {
      case ScriptEncounterState::Idle:
        return "idle";
      case ScriptEncounterState::Active:
        return "active";
      case ScriptEncounterState::Success:
        return "success";
    }
    return "idle";
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// LuauArg

  void LuauArg::push( lua_State* L ) const
  {
    std::visit( [ L ]( const auto& v )
    {
      using T = std::decay_t< decltype( v ) >;
      if constexpr( std::is_same_v< T, std::monostate > )
        lua_pushnil( L );
      else if constexpr( std::is_same_v< T, double > )
        lua_pushnumber( L, v );
      else if constexpr( std::is_same_v< T, bool > )
        lua_pushboolean( L, v ? 1 : 0 );
      else if constexpr( std::is_same_v< T, uint64_t > )
        Bind::Stack< uint64_t >::push( L, v );
      else if constexpr( std::is_same_v< T, std::string > )
        lua_pushlstring( L, v.data(), v.size() );
      else if constexpr( std::is_same_v< T, Common::Vector3 > )
        lua_pushvector( L, v.x, v.y, v.z );
      else if constexpr( std::is_same_v< T, Entity::PlayerPtr > )
        Bindings::pushPlayer( L, v );
      else if constexpr( std::is_same_v< T, Entity::BNpcPtr > )
        Bindings::pushBNpc( L, v );
      else if constexpr( std::is_same_v< T, Entity::EventObjectPtr > )
        Bindings::pushEObj( L, v );
      else if constexpr( std::is_same_v< T, Entity::CharaPtr > )
        Bindings::pushChara( L, v );
      else if constexpr( std::is_same_v< T, World::Action::Action* > )
        Bindings::pushAction( L, v );
      else
        Bindings::pushStatusEffect( L, v );
    }, v );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// lifecycle

  LuauScriptMgr::LuauScriptMgr()
  {
    m_scheduler = std::make_unique< LuauScheduler >( m_runtime, *this );
    s_instance = this;
  }

  LuauScriptMgr::~LuauScriptMgr()
  {
    shutdown();
    m_records.clear();
    m_registry.clear();
    if( s_instance == this )
      s_instance = nullptr;
  }

  LuauScriptMgr* LuauScriptMgr::instance()
  {
    return s_instance;
  }

  void LuauScriptMgr::buildPolicies()
  {
    m_policies = {};
    std::stringstream ss( m_config.luauReadOnlyGlobals );
    std::string item;
    while( std::getline( ss, item, ',' ) )
    {
      auto name = toLower( trim( item ) );
      if( name.empty() )
        continue;
      bool known = false;
      for( size_t k = 1; k < static_cast< size_t >( ScriptKind::Count ); ++k )
      {
        if( name == scriptKindName( static_cast< ScriptKind >( k ) ) )
        {
          m_policies[ k ].readOnlyGlobals = true;
          known = true;
        }
      }
      if( !known )
        Logger::warn( "[Luau] LuauReadOnlyGlobals: unknown script type '{}'", name );
    }

    std::string enabled;
    for( size_t k = 1; k < static_cast< size_t >( ScriptKind::Count ); ++k )
    {
      if( !m_policies[ k ].readOnlyGlobals )
        continue;
      if( !enabled.empty() )
        enabled += ", ";
      enabled += scriptKindName( static_cast< ScriptKind >( k ) );
    }
    Logger::info( "[Luau] read-only globals: {}", enabled.empty() ? std::string( "none" ) : enabled );
  }

  void LuauScriptMgr::init( const Common::Config::WorldConfig::Scripts& config, NativeLookup hasNative )
  {
    m_config = config;
    m_hasNative = std::move( hasNative );
    m_root = config.luauPath.empty() ? LuauRuntime::defaultScriptRoot() : fs::path( config.luauPath );
    m_filesScanned = false;
    m_fileTimes.clear();
    m_pendingFiles.clear();

    std::error_code ec;
    if( !fs::is_directory( m_root, ec ) )
    {
      Logger::warn( "[Luau] scripts directory not found: {} - Luau scripting disabled", m_root.string() );
      m_enabled = false;
      return;
    }
    auto canonical = fs::weakly_canonical( m_root, ec );
    if( ec )
    {
      Logger::warn( "[Luau] cannot resolve the scripts directory {}: {} - Luau scripting disabled", m_root.string(),
                    ec.message() );
      m_enabled = false;
      return;
    }
    m_root = canonical;
    m_enabled = true;
    buildPolicies();

    if( !m_bindingsBuilt )
    {
      Bindings::bindCore( m_bindings );
      Bindings::bindCommon( m_bindings );
      Bindings::bindEnums( m_bindings );
      Bindings::bindActors( m_bindings );
      Bindings::bindPlayer( m_bindings );
      Bindings::bindQuest( m_bindings );
      Bindings::bindScope( m_bindings );
      Bindings::bindWorld( m_bindings );
      Bindings::bindAction( m_bindings );
      Bindings::bindStatus( m_bindings );
      m_bindingsBuilt = true;
    }

    startVm();
  }

  void LuauScriptMgr::shutdown()
  {
    if( m_runtime.running() )
      stopVm();
  }

  bool LuauScriptMgr::running() const
  {
    return m_enabled && m_runtime.running();
  }

  LuauRuntime& LuauScriptMgr::runtime()
  {
    return m_runtime;
  }

  LuauScheduler& LuauScriptMgr::scheduler()
  {
    return *m_scheduler;
  }

  bool LuauScriptMgr::startVm()
  {
    if( !m_runtime.start( m_root / "common.luau", m_root, [ this ]( lua_State* L ) { m_bindings.install( L ); } ) )
      return false;

    m_runtime.setTimeBudgetMs( m_config.luauTimeBudgetMs );
    if( m_config.luauTimeBudgetMs == 0 )
      Logger::info( "[Luau] time budget: disabled" );
    else
      Logger::info( "[Luau] time budget: {} ms", m_config.luauTimeBudgetMs );

    for( auto& line : m_bindings.verify( m_runtime.state() ) )
      Logger::error( "[Luau] bindings: {}", line );

    for( auto& [ kind, list ] : m_bindings.handlerSets )
      for( auto& [ name, sig ] : list )
        if( scriptHandlerFromName( name.c_str() ) == ScriptHandler::Count )
          Logger::error( "[Luau] bindings: handler '{}' of {} scripts is missing from ScriptHandler (LuauContext.h)", name,
                         kind );
    for( size_t i = 0; i < static_cast< size_t >( ScriptHandler::Count ); ++i )
    {
      const char* name = scriptHandlerName( static_cast< ScriptHandler >( i ) );
      bool declared = false;
      for( auto& [ kind, list ] : m_bindings.handlerSets )
        for( auto& [ declaredName, sig ] : list )
          declared = declared || declaredName == name;
      if( !declared )
        Logger::error( "[Luau] bindings: handler '{}' is not declared for any script kind (Bindings/BindCommon.cpp)", name );
    }

    auto typesFile = m_root / "types" / "sapphire.d.luau";
    auto defs = m_bindings.definitions();
    std::string current;
    if( !readText( typesFile, current ) || current != defs )
    {
      if( m_config.hotSwap )
      {
        if( writeText( typesFile, defs ) )
          Logger::info( "[Luau] regenerated types/sapphire.d.luau" );
        else
          Logger::error( "[Luau] could not write {}", typesFile.string() );
      }
      else
        Logger::warn( "[Luau] types/sapphire.d.luau is out of date; run '!script luau types' or start once with HotSwap = true" );
    }

    scan();

    for( auto& [ guid, rec ] : m_records )
    {
      if( rec->scope.territory.expired() )
        continue;
      rec->dataRef = LuauSnapshot::restore( m_runtime, rec->savedData );
      rec->savedData = {};
      auto entry = findEntry( rec->kind, rec->directorId );
      if( !rec->entry || rec->entry->retired || !entry )
      {
        rec->faulted = true;
        Logger::error( "[Luau] instance #{} lost its script ({} was removed); its triggers are ignored", guid,
                       rec->entry ? m_runtime.relativePath( rec->entry->path ) : std::string( "?" ) );
        continue;
      }
      rec->entry = entry;
      buildRecordContext( *rec, false );
    }
    m_scheduler->drain();
    return true;
  }

  void LuauScriptMgr::stopVm()
  {
    for( auto& [ key, entry ] : m_registry )
      entry->singleton.reset();
    for( auto& [ guid, rec ] : m_records )
    {
      rec->ctx.reset();
      if( rec->dataRef >= 0 )
      {
        std::vector< std::string > dropped;
        rec->savedData = LuauSnapshot::capture( m_runtime, rec->dataRef, dropped );
        logDropped( guid, dropped, "VM restart" );
        LuauSnapshot::release( m_runtime, rec->dataRef );
        rec->dataRef = -1;
      }
    }
    m_registry.clear();
    m_byPath.clear();
    m_pendingReloads.clear();
    // threads whose context went away while they were running; they must not outlive the VM
    m_scheduler->forEach( [ this ]( ScriptThread& t ) { m_scheduler->cancel( t.id ); } );
    m_scenes.clear();
    m_deferredActions.clear();
    m_actionWaits.clear();
    m_failedScripts.clear();
    m_liveHooks.clear();
    m_runtime.stop();
  }

  VmRestart LuauScriptMgr::restartVm()
  {
    if( !m_enabled )
      return VmRestart::Disabled;
    // stopping closes the lua_State a running segment is executing on
    if( !m_scheduler->idle() )
    {
      Logger::info( "[Luau] VM restart deferred until the running script segment ends" );
      m_restartPending = true;
      return VmRestart::Deferred;
    }
    Logger::info( "[Luau] restarting the VM" );
    if( m_runtime.running() )
      stopVm();
    return startVm() ? VmRestart::Restarted : VmRestart::Failed;
  }

  void LuauScriptMgr::update( uint64_t nowMs )
  {
    if( !m_enabled )
      return;

    if( m_restartPending )
    {
      m_restartPending = false;
      restartVm();
    }

    if( m_runtime.running() && !m_pendingReloads.empty() )
    {
      auto reloads = std::move( m_pendingReloads );
      m_pendingReloads.clear();
      for( auto& weak : reloads )
      {
        auto entry = weak.lock();
        // skip entries removed or replaced since the request
        if( entry && findEntry( entry->kind, entry->id ) == entry )
          reloadScript( entry );
      }
    }

    if( m_runtime.running() )
    {
      // every singleton script runs on the shared clock: one ordered timer set, only due timers are touched
      m_scheduler->updateTimers( m_scheduler->sharedClock(), nowMs );

      auto lists = std::move( m_deferredCleanup );
      m_deferredCleanup.clear();
      for( auto& list : lists )
        cleanupEvents( list );

      auto actions = std::move( m_deferredActions );
      m_deferredActions.clear();
      for( auto& d : actions )
      {
        auto wait = m_actionWaits.find( d.threadId );
        if( wait == m_actionWaits.end() || wait->second != d.token )
          continue;
        m_actionWaits.erase( wait );
        auto* t = m_scheduler->find( d.threadId );
        if( !t || t->wait != WaitKind::Action )
          continue;
        m_scheduler->resume( *t, [ finished = d.finished ]( lua_State* co )
        {
          lua_pushboolean( co, finished ? 1 : 0 );
          return 1;
        } );
      }
      std::vector< uint32_t > gone;
      for( auto& [ guid, rec ] : m_records )
        if( rec->scope.territory.expired() )
          gone.push_back( guid );
      for( auto guid : gone )
        destroyRecord( guid );
    }

    if( m_config.hotSwap )
      pollFiles( nowMs );

    if( m_runtime.running() )
      m_scheduler->drain();
  }

  uint64_t LuauScriptMgr::beginActionWait( uint32_t threadId )
  {
    auto token = m_nextActionToken++;
    m_actionWaits[ threadId ] = token;
    return token;
  }

  void LuauScriptMgr::deferActionResume( uint32_t threadId, uint64_t token, bool finished )
  {
    for( auto& d : m_deferredActions )
      if( d.token == token )
        return;
    m_deferredActions.push_back( DeferredAction{ threadId, token, finished } );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// registry

  void LuauScriptMgr::scan()
  {
    std::vector< fs::path > files;
    for( auto dir : kScriptDirs )
    {
      auto base = m_root / dir;
      std::error_code ec;
      if( !fs::is_directory( base, ec ) )
        continue;
      for( auto it = fs::recursive_directory_iterator( base, ec ); !ec && it != fs::recursive_directory_iterator();
           it.increment( ec ) )
      {
        auto name = it->path().filename().string();
        if( it->is_directory() && ( name == "data" || name == "lib" ) )
        {
          it.disable_recursion_pending();
          continue;
        }
        if( it->is_regular_file() && it->path().extension() == ".luau" )
          files.push_back( it->path() );
      }
    }
    std::sort( files.begin(), files.end() );

    for( auto& f : files )
      if( auto ctx = discover( f ) )
        registerScript( std::move( ctx ), false );

    std::array< size_t, static_cast< size_t >( ScriptKind::Count ) > counts{};
    for( auto& [ key, entry ] : m_registry )
      ++counts[ static_cast< size_t >( key.first ) ];
    std::string text;
    for( size_t k = 1; k < counts.size(); ++k )
    {
      if( !text.empty() )
        text += " ";
      text += std::string( scriptKindName( static_cast< ScriptKind >( k ) ) ) + "=" + std::to_string( counts[ k ] );
    }
    Logger::info( "[Luau] loaded {} scripts ({})", m_registry.size(), text );
  }

  std::unique_ptr< LuauContext > LuauScriptMgr::discover( const fs::path& file )
  {
    auto rel = m_runtime.relativePath( file );
    m_failedScripts.erase( pathKey( file ) );
    const auto& chunk = m_runtime.compile( file );
    if( !chunk.error.empty() )
    {
      Logger::error( "[Luau] {}: {}", rel, chunk.error );
      return nullptr;
    }

    auto ctx = std::make_unique< LuauContext >( m_runtime, *m_scheduler, ScriptKind::None, 0, file );
    if( !ctx->open( true ) )
    {
      if( !ctx->lastError().empty() )
        Logger::error( "[Luau] {}: {}", rel, ctx->lastError() );
      // retried when one of the files it required changes (pollFiles)
      m_failedScripts[ pathKey( file ) ] = ctx->dependencies();
      return nullptr;
    }
    if( ctx->declaredKind() == ScriptKind::None )
    {
      Logger::warn( "[Luau] {} declares no script kind; skipped", rel );
      return nullptr;
    }
    warnUnknownHandlers( *ctx );
    return ctx;
  }

  std::shared_ptr< ScriptEntry > LuauScriptMgr::registerScript( std::unique_ptr< LuauContext > ctx, bool announce )
  {
    auto kind = ctx->declaredKind();
    auto id = ctx->declaredId();
    auto rel = ctx->relPath();
    RegistryKey key{ kind, id };

    auto existing = m_registry.find( key );
    if( existing != m_registry.end() && existing->second->key != pathKey( ctx->path() ) )
    {
      Logger::error( "[Luau] {} duplicates {} #{:08X}; skipped", rel, scriptKindName( kind ), id );
      return nullptr;
    }

    auto entry = std::make_shared< ScriptEntry >();
    entry->kind = kind;
    entry->id = id;
    entry->path = ctx->path();
    entry->key = pathKey( entry->path );
    entry->deps = ctx->dependencies();

    if( perInstance( kind ) )
      ctx.reset();
    else
    {
      ctx->setIdentity( kind, id );
      ctx->policy = m_policies[ static_cast< size_t >( kind ) ];
      if( ctx->policy.readOnlyGlobals )
        ctx->freezeGlobals();
      m_scheduler->useSharedClock( *ctx, Common::Util::getTimeMs() );
      entry->singleton = std::move( ctx );
    }
    m_registry[ key ] = entry;
    m_byPath[ entry->key ] = entry;

    if( m_hasNative && m_hasNative( kind, id ) )
      Logger::info( "[Luau] {} shadows native {} #{:08X}", rel, scriptKindName( kind ), id );

    if( announce && perInstance( kind ) )
    {
      size_t n = 0;
      for( auto& [ guid, rec ] : m_records )
      {
        if( rec->kind != kind || rec->directorId != id || !rec->entry || !rec->entry->retired )
          continue;
        rec->entry = entry;
        rec->ctx.reset();
        std::vector< std::string > dropped;
        LuauSnapshot::stripCode( m_runtime, rec->dataRef, dropped );
        logDropped( guid, dropped, "reload" );
        if( buildRecordContext( *rec, false ) )
          ++n;
      }
      if( n > 0 )
        Logger::info( "[Luau] reloaded {} ({} contexts)", rel, n );
    }
    return entry;
  }

  void LuauScriptMgr::unregisterScript( const std::shared_ptr< ScriptEntry >& entry )
  {
    auto rel = m_runtime.relativePath( entry->path );
    m_registry.erase( RegistryKey{ entry->kind, entry->id } );
    m_byPath.erase( entry->key );
    bool native = m_hasNative && m_hasNative( entry->kind, entry->id );
    Logger::info( "[Luau] {} removed; {} #{:08X} {}", rel, scriptKindName( entry->kind ), entry->id,
                  native ? "falls back to the native script" : "has no native script and is now unscripted" );

    if( perInstance( entry->kind ) )
    {
      entry->retired = true;
      size_t live = 0;
      for( auto& [ guid, rec ] : m_records )
        if( rec->entry == entry )
          ++live;
      if( live > 0 )
        Logger::info( "[Luau] {} live instance(s) keep running the removed version", live );
    }
    else
      entry->singleton.reset();
  }

  void LuauScriptMgr::reloadScript( const std::shared_ptr< ScriptEntry >& entry )
  {
    // a reload replaces contexts a running segment may be executing in
    if( !m_scheduler->idle() )
    {
      m_pendingReloads.push_back( entry );
      return;
    }
    auto rel = m_runtime.relativePath( entry->path );
    const auto& chunk = m_runtime.compile( entry->path );
    if( !chunk.error.empty() )
    {
      Logger::error( "[Luau] {}: {} - keeping the running version", rel, chunk.error );
      return;
    }

    auto probe = discover( entry->path );
    if( !probe )
    {
      Logger::error( "[Luau] {}: reload failed - keeping the running version", rel );
      return;
    }
    if( probe->declaredKind() != entry->kind || probe->declaredId() != entry->id )
    {
      // the declaration changed: re-key the script
      unregisterScript( entry );
      registerScript( std::move( probe ), true );
      return;
    }
    entry->deps = probe->dependencies();

    size_t n = 0;
    if( perInstance( entry->kind ) )
    {
      probe.reset();
      for( auto& [ guid, rec ] : m_records )
      {
        if( rec->entry != entry )
          continue;
        rec->ctx.reset();
        std::vector< std::string > dropped;
        LuauSnapshot::stripCode( m_runtime, rec->dataRef, dropped );
        logDropped( guid, dropped, "reload" );
        if( buildRecordContext( *rec, false ) )
          ++n;
      }
    }
    else
    {
      probe->setIdentity( entry->kind, entry->id );
      probe->policy = m_policies[ static_cast< size_t >( entry->kind ) ];
      if( probe->policy.readOnlyGlobals )
        probe->freezeGlobals();
      m_scheduler->useSharedClock( *probe, Common::Util::getTimeMs() );
      entry->singleton = std::move( probe );
      n = 1;
    }
    Logger::info( "[Luau] reloaded {} ({} contexts)", rel, n );
  }

  const std::shared_ptr< ScriptEntry >& LuauScriptMgr::findEntry( ScriptKind kind, uint32_t id ) const
  {
    static const std::shared_ptr< ScriptEntry > none;
    auto it = m_registry.find( RegistryKey{ kind, id } );
    return it == m_registry.end() ? none : it->second;
  }

  std::shared_ptr< ScriptEntry > LuauScriptMgr::entryByPath( const std::string& path ) const
  {
    auto it = m_byPath.find( path );
    return it == m_byPath.end() ? nullptr : it->second;
  }

  bool LuauScriptMgr::isScriptDir( const fs::path& file ) const
  {
    std::error_code ec;
    auto rel = fs::relative( file, m_root, ec );
    if( ec || rel.empty() )
      return false;
    auto it = rel.begin();
    if( it == rel.end() )
      return false;
    auto first = it->string();
    bool known = false;
    for( auto dir : kScriptDirs )
      known |= first == dir;
    if( !known )
      return false;
    for( ; it != rel.end(); ++it )
      if( it->string() == "data" || it->string() == "lib" )
        return false;
    return true;
  }

  void LuauScriptMgr::warnUnknownHandlers( LuauContext& ctx )
  {
    lua_State* env = ctx.envThread();
    if( !env )
      return;
    auto kind = ctx.kind() != ScriptKind::None ? ctx.kind() : ctx.declaredKind();
    std::vector< std::string > unknown;
    lua_pushnil( env );
    while( lua_next( env, LUA_GLOBALSINDEX ) != 0 )
    {
      if( lua_type( env, -2 ) == LUA_TSTRING && lua_isfunction( env, -1 ) )
      {
        std::string name = lua_tostring( env, -2 );
        bool looksLikeHandler = ( name.size() > 2 && name[ 0 ] == 'o' && name[ 1 ] == 'n' &&
                                  std::isupper( static_cast< unsigned char >( name[ 2 ] ) ) ) ||
                                name == "getQuestAvailability";
        if( looksLikeHandler && !m_bindings.isHandler( scriptKindName( kind ), name.c_str() ) )
          unknown.push_back( name );
      }
      lua_pop( env, 1 );
    }
    for( auto& name : unknown )
      Logger::warn( "[Luau] {}: '{}' is not a handler of {} scripts", ctx.relPath(), name, scriptKindName( kind ) );
  }

  void LuauScriptMgr::logDropped( uint32_t guid, const std::vector< std::string >& dropped, const char* moment )
  {
    for( auto& d : dropped )
      Logger::warn( "[Luau] instance #{}: director.data.{} dropped on {}", guid, d, moment );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// hot reload

  void LuauScriptMgr::pollFiles( uint64_t nowMs )
  {
    if( m_filesScanned && nowMs - m_lastPollMs < 500 )
      return;
    m_lastPollMs = nowMs;

    std::map< std::string, fs::file_time_type > current;
    std::error_code ec;
    for( auto it = fs::recursive_directory_iterator( m_root, ec ); !ec && it != fs::recursive_directory_iterator();
         it.increment( ec ) )
    {
      std::error_code fec;
      if( it->is_regular_file( fec ) && it->path().extension() == ".luau" )
      {
        // the time cached in the directory entry: no second file system query per file where the OS lists it
        auto t = it->last_write_time( fec );
        if( !fec )
          current[ pathKey( it->path() ) ] = t;
      }
    }

    if( !m_filesScanned )
    {
      m_fileTimes = std::move( current );
      m_filesScanned = true;
      return;
    }

    std::vector< std::string > deleted;
    std::vector< std::string > changed;
    for( auto& [ file, time ] : m_fileTimes )
      if( !current.count( file ) )
        deleted.push_back( file );
    for( auto& [ file, time ] : current )
    {
      auto it = m_fileTimes.find( file );
      if( it != m_fileTimes.end() && it->second == time )
      {
        m_pendingFiles.erase( file );
        continue;
      }
      // act only on files whose mtime has been stable for at least 200 ms
      auto pending = m_pendingFiles.find( file );
      if( pending == m_pendingFiles.end() || pending->second.mtime != time )
      {
        m_pendingFiles[ file ] = PendingFile{ time, nowMs };
        continue;
      }
      if( nowMs - pending->second.firstSeenMs < 200 )
        continue;
      m_pendingFiles.erase( pending );
      m_fileTimes[ file ] = time;
      changed.push_back( file );
    }
    for( auto& file : deleted )
    {
      m_fileTimes.erase( file );
      m_pendingFiles.erase( file );
    }

    auto common = pathKey( m_root / "common.luau" );
    if( std::find( changed.begin(), changed.end(), common ) != changed.end() )
    {
      restartVm();
      return;
    }
    if( !m_runtime.running() )
      return;

    auto typesDir = pathKey( m_root / "types" ) + "/";

    // deletions first, so a moved or renamed file retires and immediately re-adopts its live instances
    for( auto& file : deleted )
    {
      m_failedScripts.erase( file );
      if( auto entry = entryByPath( file ) )
        unregisterScript( entry );
    }

    std::set< ScriptEntry* > reloaded;
    std::set< std::string > retried;
    for( auto& file : changed )
    {
      if( file.rfind( typesDir, 0 ) == 0 )
        continue;

      // scripts whose discovery failed (e.g. a broken required lib) are retried once the file or a dependency changes
      std::vector< std::string > retry;
      for( auto& [ path, deps ] : m_failedScripts )
        if( path == file || deps.count( file ) )
          retry.push_back( path );
      for( auto& path : retry )
      {
        if( !retried.insert( path ).second )
          continue;
        if( auto entry = entryByPath( path ) )
        {
          if( reloaded.insert( entry.get() ).second )
            reloadScript( entry );
        }
        else if( auto ctx = discover( fs::path( path ) ) )
          registerScript( std::move( ctx ), true );
      }

      if( auto entry = entryByPath( file ) )
      {
        if( reloaded.insert( entry.get() ).second )
          reloadScript( entry );
        continue;
      }

      std::vector< std::shared_ptr< ScriptEntry > > dependents;
      for( auto& [ key, entry ] : m_registry )
        if( entry->deps.count( file ) )
          dependents.push_back( entry );
      for( auto& entry : dependents )
        if( reloaded.insert( entry.get() ).second )
          reloadScript( entry );

      if( dependents.empty() && !retried.count( file ) && isScriptDir( fs::path( file ) ) )
        if( auto ctx = discover( fs::path( file ) ) )
          registerScript( std::move( ctx ), true );
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// records

  InstanceRecord* LuauScriptMgr::findRecord( uint32_t guid )
  {
    auto it = m_records.find( guid );
    return it == m_records.end() ? nullptr : it->second.get();
  }

  InstanceRecord* LuauScriptMgr::currentRecord()
  {
    auto* t = m_scheduler->current();
    if( !t || !t->ctx )
      return nullptr;
    return static_cast< InstanceRecord* >( t->ctx->owner );
  }

  void LuauScriptMgr::notifyChange( InstanceRecord& record )
  {
    if( record.ctx )
      m_scheduler->notifyChange( *record.ctx );
  }

  InstanceRecord& LuauScriptMgr::createRecord( ScriptKind kind, Territory& territory,
                                               const std::shared_ptr< ScriptEntry >& entry )
  {
    auto rec = std::make_unique< InstanceRecord >();
    rec->kind = kind;
    rec->guid = territory.getGuId();
    rec->entry = entry;
    rec->dataRef = LuauSnapshot::newTable( m_runtime );
    auto& ref = *rec;
    m_records[ rec->guid ] = std::move( rec );
    return ref;
  }

  bool LuauScriptMgr::buildRecordContext( InstanceRecord& record, bool runCreate )
  {
    record.ctx.reset();
    if( !record.entry )
    {
      record.faulted = true;
      return false;
    }

    auto rel = m_runtime.relativePath( record.entry->path );
    auto ctx = std::make_unique< LuauContext >( m_runtime, *m_scheduler, record.kind, record.directorId,
                                                record.entry->path );
    ctx->clock().nowMs = Common::Util::getTimeMs();
    ctx->policy = m_policies[ static_cast< size_t >( record.kind ) ];
    if( !ctx->open( false ) )
    {
      if( !ctx->lastError().empty() )
        Logger::error( "[Luau] {}: {}", rel, ctx->lastError() );
      record.faulted = true;
      Logger::error( "[Luau] instance #{} ({}) failed to load; its triggers are ignored until the next reload",
                     record.guid, rel );
      return false;
    }
    ctx->owner = &record;
    record.faulted = false;
    record.ctx = std::move( ctx );

    auto& c = *record.ctx;
    if( runCreate && !record.created )
    {
      record.created = true;
      call( c, ScriptHandler::onCreate, nullptr, 0, {} );
    }
    if( record.ctx.get() == &c )
      call( c, ScriptHandler::onStart, nullptr, 0, {} );
    return true;
  }

  void LuauScriptMgr::destroyRecord( uint32_t guid )
  {
    auto it = m_records.find( guid );
    if( it == m_records.end() )
      return;
    auto rec = std::move( it->second );
    m_records.erase( it );
    rec->ctx.reset();
    LuauSnapshot::release( m_runtime, rec->dataRef );
    rec->dataRef = -1;
  }

  bool LuauScriptMgr::recordHook( uint32_t guid, ScriptHandler handler, Entity::Player* owner, uint32_t eventId, ArgList args )
  {
    auto* rec = findRecord( guid );
    if( !rec || !rec->ctx || rec->faulted )
      return false;
    return call( *rec->ctx, handler, owner, eventId, [ &args ]( lua_State* co )
    {
      for( auto& a : args )
        a.push( co );
      return static_cast< int >( args.size() );
    } );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// dispatch

  bool LuauScriptMgr::spawnHandler( LuauContext& ctx, int fnRef, Entity::Player* owner, uint32_t eventId,
                                    const std::function< int( lua_State* ) >& pushArgs )
  {
    auto* t = m_scheduler->create( ctx, owner ? owner->getId() : 0 );
    lua_getref( t->co, fnRef );
    int n = pushArgs ? pushArgs( t->co ) : 0;
    if( owner && eventId )
      if( auto ev = owner->getEvent( eventId ) )
        t->events.push_back( OwnedEvent{ owner->getAsPlayer(), eventId, ev } );
    m_scheduler->start( *t, n );
    return true;
  }

  bool LuauScriptMgr::call( LuauContext& ctx, ScriptHandler handler, Entity::Player* owner, uint32_t eventId,
                            const std::function< int( lua_State* ) >& pushArgs )
  {
    int ref = ctx.handlerRef( handler ); // LUA_NOREF for a closed context
    if( ref == LUA_NOREF )
      return false;
    return spawnHandler( ctx, ref, owner, eventId, pushArgs );
  }

  ScriptEntry* LuauScriptMgr::script( ScriptKind kind, uint32_t id ) const
  {
    // the registry is empty while the VM is down
    return findEntry( kind, id ).get();
  }

  ScriptEntry* LuauScriptMgr::questScript( uint32_t eventId ) const
  {
    return script( ScriptKind::Quest, normaliseScriptId( ScriptKind::Quest, eventId ) );
  }

  bool LuauScriptMgr::ownsInstance( const InstanceContent& instance ) const
  {
    auto it = m_records.find( instance.getGuId() );
    return it != m_records.end() && it->second->kind == ScriptKind::Instance;
  }

  bool LuauScriptMgr::ownsBattle( const QuestBattle& battle ) const
  {
    auto it = m_records.find( battle.getGuId() );
    return it != m_records.end() && it->second->kind == ScriptKind::QuestBattle;
  }

  bool LuauScriptMgr::eventHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId,
                                 ArgList extra )
  {
    if( !script.singleton )
      return false;
    auto pp = player.getAsPlayer();
    return call( *script.singleton, handler, &player, eventId, [ & ]( lua_State* co )
    {
      Bindings::pushPlayer( co, pp );
      lua_pushnumber( co, eventId );
      for( auto& a : extra )
        a.push( co );
      return static_cast< int >( 2 + extra.size() );
    } );
  }

  bool LuauScriptMgr::questHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId,
                                 bool requireActive, ArgList extra )
  {
    if( !script.singleton )
      return false;
    if( requireActive && !player.hasQuest( eventId ) )
      return false;
    auto pp = player.getAsPlayer();
    auto questId = static_cast< uint16_t >( eventId & 0xFFFF );
    return call( *script.singleton, handler, &player, eventId, [ & ]( lua_State* co )
    {
      Bindings::pushQuest( co, pp, questId );
      Bindings::pushPlayer( co, pp );
      for( auto& a : extra )
        a.push( co );
      return static_cast< int >( 2 + extra.size() );
    } );
  }

  bool LuauScriptMgr::zoneHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId,
                                ArgList extra )
  {
    // same arguments as an event handler
    return eventHook( script, handler, player, eventId, extra );
  }

  bool LuauScriptMgr::eobjHook( ScriptEntry& script, Entity::Player& player, Entity::EventObjectPtr eobj,
                                uint32_t eventId )
  {
    if( !script.singleton )
      return false;
    auto pp = player.getAsPlayer();
    return call( *script.singleton, ScriptHandler::onTalk, &player, eventId, [ & ]( lua_State* co )
    {
      Bindings::pushPlayer( co, pp );
      Bindings::pushEObj( co, eobj );
      lua_pushnumber( co, eventId );
      return 3;
    } );
  }

  bool LuauScriptMgr::syncHook( ScriptEntry& script, ScriptHandler handler, ArgList args )
  {
    if( !script.singleton )
      return false;
    auto& ctx = *script.singleton;
    int ref = ctx.handlerRef( handler );
    if( ref == LUA_NOREF )
      return false;

    auto serial = beginHook();
    auto* t = m_scheduler->syncThread( ctx, 0 );
    lua_getref( t->co, ref );
    for( auto& a : args )
      a.push( t->co );
    auto errorsBefore = m_errorCount;
    bool ok = m_scheduler->runSync( *t, static_cast< int >( args.size() ) );
    endHook( serial );
    if( !ok && m_errorCount == errorsBefore )
      Logger::error( "[Luau] {}: {} must not yield", ctx.relPath(), scriptHandlerName( handler ) );
    return true;
  }

  uint8_t LuauScriptMgr::getQuestAvailability( ScriptEntry& script, Entity::Player& player, uint32_t questId )
  {
    constexpr auto available = static_cast< uint8_t >( Event::EventHandler::QuestAvailability::Available );
    if( !script.singleton )
      return available;
    auto& ctx = *script.singleton;
    int ref = ctx.handlerRef( ScriptHandler::getQuestAvailability );
    if( ref == LUA_NOREF )
      return available;

    auto* t = m_scheduler->syncThread( ctx, player.getId() );
    lua_getref( t->co, ref );
    Bindings::pushPlayer( t->co, player.getAsPlayer() );
    lua_pushnumber( t->co, questId );
    uint8_t result = available;
    auto errorsBefore = m_errorCount;
    bool ok = m_scheduler->runSync( *t, 2, [ &result ]( lua_State* co )
    {
      if( lua_gettop( co ) < 1 || !lua_isnumber( co, -1 ) )
        return;
      double value = lua_tonumber( co, -1 );
      if( value >= 0 && value <= 255 )
        result = static_cast< uint8_t >( value );
    } );
    if( !ok )
    {
      if( m_errorCount == errorsBefore )
        Logger::error( "[Luau] getQuestAvailability must return synchronously" );
      return available;
    }
    return result;
  }

  uint64_t LuauScriptMgr::beginHook()
  {
    auto serial = m_nextHookSerial++;
    m_liveHooks.push_back( serial );
    return serial;
  }

  void LuauScriptMgr::endHook( uint64_t serial )
  {
    m_liveHooks.erase( std::remove( m_liveHooks.begin(), m_liveHooks.end(), serial ), m_liveHooks.end() );
  }

  bool LuauScriptMgr::isHookLive( uint64_t serial ) const
  {
    return serial != 0 && std::find( m_liveHooks.begin(), m_liveHooks.end(), serial ) != m_liveHooks.end();
  }

  uint64_t LuauScriptMgr::currentHookSerial() const
  {
    return m_liveHooks.empty() ? 0 : m_liveHooks.back();
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// instance content

  bool LuauScriptMgr::onInstanceInit( InstanceContent& instance )
  {
    if( !running() )
      return false;
    auto entry = findEntry( ScriptKind::Instance, instance.getDirectorId() );
    if( !entry )
      return false;
    auto p = instance.getAsInstanceContent();
    if( !p )
      return false;

    auto& rec = createRecord( ScriptKind::Instance, instance, entry );
    rec.directorId = instance.getDirectorId();
    std::weak_ptr< InstanceContent > weak = p;
    rec.scope.territory = p;
    rec.scope.director = std::static_pointer_cast< Event::Director >( p );
    rec.scope.instance = p;
    rec.scope.hasDirector = true;
    rec.scope.hasInstance = true;
    rec.scope.setVar = [ weak ]( uint8_t index, uint8_t value ) { if( auto s = weak.lock() ) s->setVar( index, value ); };
    rec.scope.setSequence = [ weak ]( uint8_t value ) { if( auto s = weak.lock() ) s->setSequence( value ); };
    rec.scope.setFlags = [ weak ]( uint8_t value ) { if( auto s = weak.lock() ) s->setFlags( value ); };

    buildRecordContext( rec, true );
    m_scheduler->drain();
    return true;
  }

  void LuauScriptMgr::onInstanceDestroy( InstanceContent& instance )
  {
    if( ownsInstance( instance ) )
      destroyRecord( instance.getGuId() );
  }

  void LuauScriptMgr::onInstanceUpdate( InstanceContent& instance, uint64_t tickCount )
  {
    auto* rec = findRecord( instance.getGuId() );
    if( !rec || !running() )
      return;
    if( rec->ctx )
      m_scheduler->updateTimers( rec->ctx->clock(), tickCount );
    driveEncounter( *rec, tickCount );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceReset( InstanceContent& instance )
  {
    recordHook( instance.getGuId(), ScriptHandler::onReset, nullptr, 0, {} );
  }

  void LuauScriptMgr::onInstanceStateChange( InstanceContent& instance, uint32_t oldState, uint32_t newState )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    recordHook( instance.getGuId(), ScriptHandler::onStateChange, nullptr, 0, { double( oldState ), double( newState ) } );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceEnterTerritory( InstanceContent& instance, Entity::Player& player, uint32_t eventId,
                                                uint16_t param1, uint16_t param2 )
  {
    recordHook( instance.getGuId(), ScriptHandler::onEnterTerritory, &player, eventId,
                { player.getAsPlayer(), double( eventId ), double( param1 ), double( param2 ) } );
  }

  void LuauScriptMgr::onInstanceLeaveTerritory( InstanceContent& instance, Entity::Player& player )
  {
    recordHook( instance.getGuId(), ScriptHandler::onLeaveTerritory, &player, 0, { player.getAsPlayer() } );
  }

  void LuauScriptMgr::onInstanceDirectorVarChange( InstanceContent& instance, uint8_t var, uint8_t val )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    recordHook( instance.getGuId(), ScriptHandler::onDirectorVarChange, nullptr, 0, { double( var ), double( val ) } );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceDirectorSeqChange( InstanceContent& instance, uint8_t seq )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    recordHook( instance.getGuId(), ScriptHandler::onDirectorSeqChange, nullptr, 0, { double( seq ) } );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceDirectorFlagChange( InstanceContent& instance, uint8_t flags )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    recordHook( instance.getGuId(), ScriptHandler::onDirectorFlagChange, nullptr, 0, { double( flags ) } );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceCustomVarChange( InstanceContent& instance )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onInstanceActorDeath( InstanceContent& instance, Entity::Chara& chara )
  {
    if( auto* rec = findRecord( instance.getGuId() ) )
      notifyChange( *rec );
    if( chara.isPlayer() )
      recordHook( instance.getGuId(), ScriptHandler::onActorDeath, nullptr, 0, { chara.getAsPlayer() } );
    else if( chara.isBattleNpc() )
      recordHook( instance.getGuId(), ScriptHandler::onActorDeath, nullptr, 0, { chara.getAsBNpc() } );
  }

  bool LuauScriptMgr::onInstanceTalk( InstanceContent& instance, Entity::Player& player, Entity::EventObjectPtr eobj,
                                      uint32_t eventId, uint64_t actorId )
  {
    auto* rec = findRecord( instance.getGuId() );
    if( !rec || !rec->ctx || rec->faulted )
      return false;
    auto& ctx = *rec->ctx;
    auto pp = player.getAsPlayer();

    if( eobj )
    {
      int ref = ctx.talkHandler( eobj->getName() );
      if( ref >= 0 )
      {
        return spawnHandler( ctx, ref, &player, eventId,
                             [ & ]( lua_State* co )
                             {
                               Bindings::pushPlayer( co, pp );
                               Bindings::pushEObj( co, eobj );
                               lua_pushnumber( co, eventId );
                               return 3;
                             } );
      }
    }

    return call( ctx, ScriptHandler::onTalk, &player, eventId, [ & ]( lua_State* co )
    {
      Bindings::pushPlayer( co, pp );
      if( eobj )
        Bindings::pushEObj( co, eobj );
      else
        lua_pushnil( co );
      lua_pushnumber( co, eventId );
      Bind::Stack< uint64_t >::push( co, actorId );
      return 4;
    } );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// encounters

  void LuauScriptMgr::driveEncounter( InstanceRecord& record, uint64_t tickCount )
  {
    auto enc = record.encounter;
    if( !enc )
      return;

    enc->update( tickCount );

    bool anyBoss = false;
    bool allDead = true;
    Entity::BNpcPtr aggroed;
    Entity::CharaPtr aggroTarget;
    for( auto id : record.bossLayoutIds )
    {
      auto boss = enc->getBNpc( id );
      if( !boss )
        continue;
      anyBoss = true;
      allDead &= !boss->isAlive();
      if( aggroed )
        continue;
      if( auto target = boss->hateListGetHighest() )
      {
        aggroed = boss;
        aggroTarget = target;
      }
    }

    if( anyBoss )
    {
      if( enc->getStatus() == EncounterStatus::IDLE && aggroed )
      {
        enc->setStatus( EncounterStatus::ACTIVE );
        // the arena is not defined for every encounter; enter the aggro target and the boss explicitly
        enc->onEnterRange( aggroTarget );
        enc->onEnterRange( aggroed );
      }
      else if( allDead && enc->getStatus() != EncounterStatus::SUCCESS )
        enc->setStatus( EncounterStatus::SUCCESS );

      if( enc->getStatus() == EncounterStatus::ACTIVE && !aggroed )
        enc->setStatus( EncounterStatus::FAIL );
    }

    if( record.currentEncounter.empty() || record.currentState == ScriptEncounterState::Success )
      return;
    auto state = encounterState( enc->getStatus() );
    if( state == record.currentState )
      return;
    record.currentState = state;
    record.encounters[ record.currentEncounter ] = state;
    notifyChange( record );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// quest battles

  bool LuauScriptMgr::onBattleInit( QuestBattle& battle )
  {
    if( !running() )
      return false;
    auto entry = findEntry( ScriptKind::QuestBattle, battle.getDirectorId() );
    if( !entry )
      return false;
    auto p = battle.getAsQuestBattle();
    if( !p )
      return false;

    auto& rec = createRecord( ScriptKind::QuestBattle, battle, entry );
    rec.directorId = battle.getDirectorId();
    std::weak_ptr< QuestBattle > weak = p;
    rec.scope.territory = p;
    rec.scope.director = std::static_pointer_cast< Event::Director >( p );
    rec.scope.questBattle = p;
    rec.scope.hasDirector = true;
    rec.scope.hasQuestBattle = true;
    // QuestBattle's setters notify through ScriptMgr::onBattleChange
    rec.scope.setVar = [ weak ]( uint8_t index, uint8_t value ) { if( auto s = weak.lock() ) s->setVar( index, value ); };
    rec.scope.setSequence = [ weak ]( uint8_t value ) { if( auto s = weak.lock() ) s->setSequence( value ); };
    rec.scope.setFlags = [ weak ]( uint8_t value ) { if( auto s = weak.lock() ) s->setFlags( value ); };

    buildRecordContext( rec, true );
    m_scheduler->drain();
    return true;
  }

  void LuauScriptMgr::onBattleDestroy( QuestBattle& battle )
  {
    if( ownsBattle( battle ) )
      destroyRecord( battle.getGuId() );
  }

  void LuauScriptMgr::onBattleChange( QuestBattle& battle )
  {
    if( auto* rec = findRecord( battle.getGuId() ) )
      notifyChange( *rec );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onBattleUpdate( QuestBattle& battle, uint64_t tickCount )
  {
    auto* rec = findRecord( battle.getGuId() );
    if( !rec || !running() )
      return;
    if( rec->ctx )
      m_scheduler->updateTimers( rec->ctx->clock(), tickCount );
    driveEncounter( *rec, tickCount );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onPlayerSetup( QuestBattle& battle, Entity::Player& player )
  {
    // no owner: the zone move that runs onPlayerSetup also fires onPlayerTerritoryChanged, which cancels the player's
    // threads; the handler belongs to the battle record and ends with it
    recordHook( battle.getGuId(), ScriptHandler::onPlayerSetup, nullptr, 0, { player.getAsPlayer() } );
  }

  void LuauScriptMgr::onDutyCommence( QuestBattle& battle, Entity::Player& player )
  {
    recordHook( battle.getGuId(), ScriptHandler::onDutyCommence, &player, 0, { player.getAsPlayer() } );
  }

  void LuauScriptMgr::onBattleEnterTerritory( QuestBattle& battle, Entity::Player& player, uint32_t eventId,
                                              uint16_t param1, uint16_t param2 )
  {
    recordHook( battle.getGuId(), ScriptHandler::onEnterTerritory, &player, eventId,
                { player.getAsPlayer(), double( eventId ), double( param1 ), double( param2 ) } );
  }

  void LuauScriptMgr::onDutyComplete( QuestBattle& battle, Entity::Player& player )
  {
    recordHook( battle.getGuId(), ScriptHandler::onDutyComplete, &player, 0, { player.getAsPlayer() } );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// scenes

  uint64_t LuauScriptMgr::sceneBegin( lua_State* L, Entity::Player& player, uint32_t eventId, uint32_t scene,
                                      uint32_t flags, const std::vector< uint32_t >& params )
  {
    if( params.size() > 255 )
      luaL_error( L, "too many scene params (%d, max 255)", static_cast< int >( params.size() ) );
    auto& t = Bindings::beginYield( L );
    auto ev = player.getEvent( eventId );
    if( !ev )
      luaL_error( L, "event %08X is not open for this player; use player:nest", eventId );

    std::pair< uint32_t, uint32_t > key{ player.getId(), eventId };
    // the engine keeps one scene callback per event: a second scene would orphan the task waiting for the first
    if( auto pending = m_scenes.find( key ); pending != m_scenes.end() && pending->second.threadId != t.id )
      luaL_error( L, "event %08X already has a scene pending for this player, played by another task", eventId );

    bool owned = false;
    for( auto& o : t.events )
      owned |= o.eventId == eventId && o.handler == ev;
    if( !owned )
      t.events.push_back( OwnedEvent{ player.getAsPlayer(), eventId, ev } );

    auto token = m_nextSceneToken++;
    t.usedScenes = true;
    m_scenes[ key ] = PendingScene{ token, t.id, player.getId(), eventId, scene, player.getAsPlayer(), {} };

    if( ( eventId >> 16 ) == Event::EventHandler::EventHandlerType::Quest )
    {
      if( !params.empty() )
        ev->getScenePlayParams()->setParams( static_cast< uint16_t >( scene ), params );
      eventMgr().playQuestScene( player, eventId, scene, flags,
                                 [ this, key, token ]( World::Quest& quest, Entity::Player&, const Event::SceneResult& r )
                                 {
                                   onSceneReturn( key, token, r, &quest );
                                 } );
    }
    else
    {
      eventMgr().playScene( player, eventId, scene, flags, params,
                            [ this, key, token ]( Entity::Player&, const Event::SceneResult& r )
                            {
                              onSceneReturn( key, token, r, nullptr );
                            } );
    }
    return token;
  }

  int LuauScriptMgr::sceneAwait( lua_State* L, uint64_t token )
  {
    auto& t = Bindings::beginYield( L );
    auto it = m_scenes.begin();
    for( ; it != m_scenes.end(); ++it )
      if( it->second.token == token )
        break;
    if( it == m_scenes.end() || it->second.threadId != t.id )
      luaL_error( L, "scene is no longer pending" );

    if( !it->second.queue.empty() )
    {
      auto ev = std::move( it->second.queue.front() );
      it->second.queue.pop_front();
      if( ev.isReturn )
        m_scenes.erase( it );
      pushSceneEvent( L, ev );
      return 1;
    }
    t.wait = WaitKind::Scene;
    return lua_yield( L, 0 );
  }

  void LuauScriptMgr::sceneResume( lua_State* L, uint64_t token, uint8_t yieldId, const std::vector< uint32_t >& values )
  {
    for( auto& [ key, scene ] : m_scenes )
    {
      if( scene.token != token )
        continue;
      if( values.size() > 255 )
        luaL_error( L, "too many scene values (%d, max 255)", static_cast< int >( values.size() ) );
      auto player = scene.player.lock();
      if( !player )
        luaL_error( L, "Player is no longer valid" );
      eventMgr().resumeScene( *player, scene.eventId, scene.sceneId, yieldId, values );
      return;
    }
    luaL_error( L, "scene is no longer pending" );
  }

  void LuauScriptMgr::pushSceneEvent( lua_State* L, const SceneEvent& ev )
  {
    lua_createtable( L, 0, 6 );
    lua_pushstring( L, ev.isReturn ? "return" : "yield" );
    lua_setfield( L, -2, "kind" );
    if( ev.isReturn )
    {
      lua_pushnumber( L, ev.errorCode );
      lua_setfield( L, -2, "errorCode" );
      Bind::Stack< std::vector< uint32_t > >::push( L, ev.results );
      lua_setfield( L, -2, "results" );
    }
    else
    {
      lua_pushnumber( L, ev.yieldId );
      lua_setfield( L, -2, "yieldId" );
    }
    lua_pushlstring( L, ev.string.data(), ev.string.size() );
    lua_setfield( L, -2, "string" );
    Bind::Stack< uint64_t >::push( L, ev.intResult );
    lua_setfield( L, -2, "int" );
  }

  void LuauScriptMgr::onSceneReturn( std::pair< uint32_t, uint32_t > key, uint64_t token, const Event::SceneResult& result,
                                     World::Quest* quest )
  {
    if( !running() )
      return;
    auto it = m_scenes.find( key );
    if( it == m_scenes.end() || it->second.token != token )
      return;
    auto* t = m_scheduler->find( it->second.threadId );
    if( !t )
    {
      m_scenes.erase( it );
      return;
    }

    SceneEvent ev;
    ev.isReturn = true;
    ev.errorCode = result.errorCode;
    ev.results = result.results;
    ev.string = result.resultString;
    ev.intResult = result.intResult;

    if( t->wait == WaitKind::Scene && !t->queued && !t->running )
    {
      m_scenes.erase( it );
      // the resume happens inside the callback: handleReturnEventScene calls checkEvent right after it
      m_scheduler->resume( *t, [ this, ev ]( lua_State* co )
      {
        pushSceneEvent( co, ev );
        return 1;
      }, quest );
    }
    else
    {
      it->second.queue.push_back( std::move( ev ) );
      if( auto player = it->second.player.lock() )
        if( auto handler = player->getEvent( it->second.eventId ) )
          handler->setPlayedScene( true );
    }
  }

  bool LuauScriptMgr::onYield( Entity::Player& player, uint32_t eventId, uint16_t sceneId, uint8_t resumeId,
                               const std::string& resultString, uint64_t resultInt )
  {
    if( !running() )
      return false;
    auto it = m_scenes.find( { player.getId(), eventId } );
    if( it == m_scenes.end() || it->second.sceneId != sceneId )
      return false;
    auto* t = m_scheduler->find( it->second.threadId );
    if( !t )
    {
      m_scenes.erase( it );
      return false;
    }

    SceneEvent ev;
    ev.yieldId = resumeId;
    ev.string = resultString;
    ev.intResult = resultInt;
    if( t->wait == WaitKind::Scene && !t->queued && !t->running )
      m_scheduler->resume( *t, [ this, ev ]( lua_State* co )
      {
        pushSceneEvent( co, ev );
        return 1;
      } );
    else
      it->second.queue.push_back( std::move( ev ) );
    return true;
  }

  void LuauScriptMgr::onPlayerLogout( Entity::Player& player )
  {
    if( !running() )
      return;
    m_scheduler->cancelPlayer( player.getId() );
    m_scheduler->drain();
  }

  void LuauScriptMgr::onPlayerTerritoryChanged( Entity::Player& player )
  {
    if( !running() )
      return;
    m_scheduler->cancelPlayer( player.getId() );
    m_scheduler->drain();
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// ISchedulerHost

  void LuauScriptMgr::flushQuests( ScriptThread& thread )
  {
    auto quests = std::move( thread.quests );
    thread.quests.clear();
    for( auto& a : quests )
    {
      if( a.bound )
        continue;
      if( auto player = a.player.lock() )
        if( a.working != a.snapshot )
          player->updateQuest( a.working );
    }
  }

  void LuauScriptMgr::onYield( ScriptThread& thread )
  {
    // keeps EventMgr::checkEvent from finishing an event whose handler is still running
    for( auto& o : thread.events )
      if( auto player = o.player.lock() )
        if( player->getEvent( o.eventId ) == o.handler )
          o.handler->setPlayedScene( true );
    flushQuests( thread );
  }

  void LuauScriptMgr::onExit( ScriptThread& thread, ExitReason reason )
  {
    if( reason == ExitReason::Finished )
      flushQuests( thread );
    else
      thread.quests.clear();

    for( auto& o : thread.events )
      if( auto player = o.player.lock() )
        if( player->getEvent( o.eventId ) == o.handler )
          o.handler->setPlayedScene( false );

    if( !thread.events.empty() )
    {
      if( reason == ExitReason::Cancelled )
        cleanupEvents( thread.events );
      else
        m_deferredCleanup.push_back( std::move( thread.events ) );
      thread.events.clear();
    }

    // only threads that played scenes can have entries: sync hooks skip the scan
    if( thread.usedScenes )
    {
      for( auto it = m_scenes.begin(); it != m_scenes.end(); )
      {
        if( it->second.threadId == thread.id )
          it = m_scenes.erase( it );
        else
          ++it;
      }
    }
    m_actionWaits.erase( thread.id );
  }

  void LuauScriptMgr::cleanupEvents( std::vector< OwnedEvent >& events )
  {
    if( Common::Service< World::Manager::EventMgr >::empty() )
      return;
    // top of the stack first; only events nobody else has started a scene on since the thread released them
    for( size_t i = events.size(); i-- > 0; )
    {
      auto& o = events[ i ];
      auto player = o.player.lock();
      if( !player || player->getEvent( o.eventId ) != o.handler || o.handler->hasPlayedScene() )
        continue;
      o.handler->setPlayedScene( false );
      eventMgr().eventFinish( *player, o.eventId, i == 0 ? 1 : 0 );
    }
  }

  void LuauScriptMgr::reportError( ScriptThread& thread, const std::string& message )
  {
    ++m_errorCount;
    Logger::error( "[Luau] {}: {}", thread.ctx ? thread.ctx->relPath() : std::string( "?" ), message );
    if( thread.playerId == 0 || Common::Service< World::Manager::PlayerMgr >::empty() )
      return;
    if( auto player = Common::Service< World::Manager::PlayerMgr >::ref().getPlayer( thread.playerId ) )
      World::Manager::PlayerMgr::sendDebug( *player, "[Luau] " + firstLine( message ) );
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// debug commands

  std::vector< std::string > LuauScriptMgr::statusLines()
  {
    std::vector< std::string > out;
    if( !m_enabled )
    {
      out.emplace_back( "Luau scripting is disabled (scripts directory not found)" );
      return out;
    }
    if( !m_runtime.running() )
    {
      out.emplace_back( "Luau VM is not running (common.luau failed to load)" );
      return out;
    }

    std::array< size_t, static_cast< size_t >( ScriptKind::Count ) > counts{};
    for( auto& [ key, entry ] : m_registry )
      ++counts[ static_cast< size_t >( key.first ) ];
    std::string text;
    for( size_t k = 1; k < counts.size(); ++k )
      text += std::string( text.empty() ? "" : " " ) + scriptKindName( static_cast< ScriptKind >( k ) ) + "=" +
              std::to_string( counts[ k ] );
    out.push_back( "scripts: " + std::to_string( m_registry.size() ) + " (" + text + ")" );

    size_t live = 0;
    for( auto& [ guid, rec ] : m_records )
      live += rec->ctx ? 1 : 0;
    out.push_back( "instance contexts: " + std::to_string( live ) + ", threads: " +
                   std::to_string( m_scheduler->threadCount() ) );
    out.push_back( "VM heap: " + std::to_string( lua_gc( m_runtime.state(), LUA_GCCOUNT, 0 ) ) + " KB" );

    for( auto& [ key, entry ] : m_registry )
    {
      auto rel = m_runtime.relativePath( entry->path );
      if( m_hasNative && m_hasNative( entry->kind, entry->id ) )
        out.push_back( rel + " shadows native " + scriptKindName( entry->kind ) + " #" + hexId( entry->id ) );
    }

    std::map< ScriptEntry*, size_t > retired;
    for( auto& [ guid, rec ] : m_records )
    {
      if( rec->entry && rec->entry->retired )
        ++retired[ rec->entry.get() ];
      if( rec->faulted )
        out.push_back( "faulted: instance #" + std::to_string( guid ) + " (" +
                       ( rec->entry ? m_runtime.relativePath( rec->entry->path ) : std::string( "?" ) ) + ")" );
    }
    for( auto& [ entry, n ] : retired )
      out.push_back( "retired: " + m_runtime.relativePath( entry->path ) + " (" + std::to_string( n ) +
                     " live instance(s))" );
    return out;
  }

  std::vector< std::string > LuauScriptMgr::reloadByName( const std::string& name )
  {
    std::vector< std::string > out;
    if( !running() )
    {
      out.emplace_back( "Luau scripting is not running" );
      return out;
    }
    auto needle = toLower( name );
    std::vector< std::shared_ptr< ScriptEntry > > matches;
    for( auto& [ key, entry ] : m_registry )
      if( toLower( m_runtime.relativePath( entry->path ) ).find( needle ) != std::string::npos )
        matches.push_back( entry );

    if( matches.empty() )
      out.push_back( "No Luau script matches: " + name );
    else if( matches.size() > 1 )
    {
      out.push_back( "Several Luau scripts match '" + name + "':" );
      for( auto& e : matches )
        out.push_back( "  " + m_runtime.relativePath( e->path ) );
    }
    else if( !m_scheduler->idle() )
    {
      reloadScript( matches.front() );
      out.push_back( "Reload of " + m_runtime.relativePath( matches.front()->path ) +
                     " deferred until the running script segment ends" );
    }
    else
    {
      reloadScript( matches.front() );
      m_scheduler->drain();
      out.push_back( "Reloaded " + m_runtime.relativePath( matches.front()->path ) );
    }
    return out;
  }

  bool LuauScriptMgr::writeTypes( std::string& outPath )
  {
    auto file = m_root / "types" / "sapphire.d.luau";
    outPath = file.string();
    return writeText( file, m_bindings.definitions() );
  }

  std::vector< std::string > LuauScriptMgr::dumpLines( uint32_t guid )
  {
    std::vector< std::string > out;
    auto* rec = findRecord( guid );
    if( !rec || !running() )
    {
      out.push_back( "No Luau instance with guid " + std::to_string( guid ) );
      return out;
    }

    out.push_back( "instance #" + std::to_string( guid ) + " (" +
                   ( rec->entry ? m_runtime.relativePath( rec->entry->path ) : std::string( "?" ) ) + ")" +
                   ( rec->faulted ? " FAULTED" : "" ) );

    if( auto director = rec->scope.director.lock() )
    {
      out.push_back( "seq = " + std::to_string( director->getSequence() ) +
                     ", flags = " + std::to_string( director->getFlags() ) );
      std::string vars = "vars:";
      for( uint8_t i = 0; i < 10; ++i )
        vars += " " + std::to_string( director->getDirectorVar( i ) );
      out.push_back( vars );
    }

    out.emplace_back( "data:" );
    for( auto& line : LuauSnapshot::print( m_runtime, rec->dataRef ) )
      out.push_back( "  " + line );

    if( !rec->encounters.empty() )
    {
      out.emplace_back( "encounters:" );
      for( auto& [ name, state ] : rec->encounters )
        out.push_back( "  " + name + ": " + scriptEncounterStateName( state ) +
                       ( name == rec->currentEncounter ? " (current)" : "" ) );
    }

    if( rec->ctx )
    {
      m_scheduler->forEach( [ & ]( ScriptThread& t )
      {
        if( t.ctx != rec->ctx.get() )
          return;
        std::string trace = lua_debugtrace( t.co ) ? lua_debugtrace( t.co ) : "";
        out.push_back( "thread #" + std::to_string( t.id ) + " " + waitKindName( t.wait ) + " " + firstLine( trace ) );
      } );
    }
    return out;
  }
}
