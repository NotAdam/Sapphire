#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

struct lua_State;

namespace Sapphire::Scripting
{
  class LuauRuntime;
  class LuauScheduler;

  enum class ScriptKind : uint8_t
  {
    None,
    Instance,
    Quest,
    Event,
    QuestBattle,
    Action,
    Status,
    Zone,
    EObj,
    Count
  };

  // "instance", "quest", ... (lower case, as used in config and logs)
  const char* scriptKindName( ScriptKind kind );

  // the registered id for a declared id: instance -> 0x8003xxxx, quest -> 0x0001xxxx, quest battle -> 0x8006xxxx
  uint32_t normaliseScriptId( ScriptKind kind, uint32_t declaredId );

// Every handler the server dispatches, across all script kinds; a member's name is the Luau global it binds.
// Bindings/BindCommon.cpp declares which kinds take which handler (LuauScriptMgr::startVm reports mismatches).
#define SAPPHIRE_LUAU_HANDLERS( X ) \
  X( onCreate ) X( onStart ) X( onReset ) X( onStateChange ) X( onEnterTerritory ) X( onLeaveTerritory ) \
  X( onDirectorVarChange ) X( onDirectorSeqChange ) X( onDirectorFlagChange ) X( onActorDeath ) X( onTalk ) \
  X( onSay ) X( onEventItem ) X( onEventGroundItem ) X( onBNpcKill ) X( onTriggerOwnerDeaggro ) X( onEmote ) \
  X( onWithinRange ) X( onOutsideRange ) X( onPlayerDeath ) X( onEObjHit ) X( getQuestAvailability ) \
  X( onTradeReturn ) X( onPlayerSetup ) X( onDutyCommence ) X( onDutyComplete ) X( onBeforeBootstrap ) \
  X( onExecute ) X( onInterrupt ) X( onAfterBuildEffect ) X( onApply ) X( onTick ) X( onExpire ) \
  X( onPlayerHit ) X( onZoneInit )

  enum class ScriptHandler : uint8_t
  {
#define X( name ) name,
    SAPPHIRE_LUAU_HANDLERS( X )
#undef X
    Count
  };

  const char* scriptHandlerName( ScriptHandler handler );
  // ScriptHandler::Count when name is not a handler
  ScriptHandler scriptHandlerFromName( const char* name );

  // per-kind guards, filled from world.ini
  struct ScriptPolicy
  {
    bool readOnlyGlobals = false;
  };

  // A time base for task.wait. Singleton scripts share the scheduler's clock (advanced by LuauScriptMgr::update);
  // per-instance contexts own one, advanced only when their territory updates.
  struct ScriptClock
  {
    uint64_t nowMs = 0;
    std::set< std::pair< uint64_t, uint32_t > > timers; // (wakeAt, thread id), maintained by LuauScheduler
  };

  // One sandboxed environment: a script chunk plus the modules it requires.
  class LuauContext
  {
  public:
    LuauContext( LuauRuntime& runtime, LuauScheduler& scheduler, ScriptKind kind, uint32_t scriptId,
                 std::filesystem::path scriptPath );
    ~LuauContext();
    LuauContext( const LuauContext& ) = delete;
    LuauContext& operator=( const LuauContext& ) = delete;

    // new sandboxed env, run the script chunk to completion; false on error (see lastError())
    bool open( bool discovery );
    // cancel threads, release the env, talk handlers and module cache
    void close();
    bool isOpen() const;
    // true while close() cancels the context's threads: no new tasks may be spawned
    bool isClosing() const;
    bool discovery() const;

    // registry ref of the handler function; LUA_NOREF when the script does not define it or the context is closed
    int handlerRef( ScriptHandler handler ) const;

    ScriptKind kind() const;
    uint32_t scriptId() const;
    void setIdentity( ScriptKind kind, uint32_t scriptId );
    ScriptKind declaredKind() const;
    uint32_t declaredId() const;
    void declare( ScriptKind kind, uint32_t id );

    const std::filesystem::path& path() const;
    const std::string& relPath() const;
    const std::set< std::string >& dependencies() const;
    const std::string& lastError() const;

    // the env thread (sandboxed globals); threads created from it share the env
    lua_State* envThread() const;
    LuauRuntime& runtime() const;
    LuauScheduler& scheduler() const;

    // locks the context globals (readOnlyGlobals policy)
    void freezeGlobals();

    // on.talk registry (instance scripts)
    void addTalkHandler( const std::string& eobjName, int ref );
    int talkHandler( const std::string& eobjName ) const;
    const std::map< std::string, int >& talkHandlers() const;

    // require() support
    // LUA_NOREF when not cached; a module that returned nil is cached as LUA_REFNIL
    int moduleRef( const std::string& key ) const;
    void setModuleRef( const std::string& key, int ref );
    bool beginLoading( const std::string& key );
    void endLoading( const std::string& key );
    void addDependency( const std::string& absolutePath );

    // the time base for task.wait in this context
    ScriptClock& clock();
    uint64_t clockMs() const;

    ScriptPolicy policy;
    void* owner = nullptr;  // InstanceRecord* for per-instance contexts

    // selftest helpers
    bool startHandler( const char* name );
    bool pushGlobalFunction( lua_State* L, const char* name ) const; // by-name lookup; the server dispatches through handlerRef
    void setGlobalFromRef( const char* name, int ref );
    bool getGlobalNumber( const char* name, double& out ) const;
    void setGlobalBool( const char* name, bool value );

  private:
    friend class LuauScheduler;

    void resolveHandlers();

    LuauRuntime& m_runtime;
    LuauScheduler& m_scheduler;
    ScriptKind m_kind;
    uint32_t m_scriptId;
    ScriptKind m_declaredKind{ ScriptKind::None };
    uint32_t m_declaredId{ 0 };
    std::filesystem::path m_path;
    std::string m_relPath;
    lua_State* m_env{ nullptr };
    int m_envRef{ -1 };
    bool m_discovery{ false };
    bool m_closing{ false };
    std::string m_lastError;
    std::set< std::string > m_dependencies;
    std::map< std::string, int > m_talkHandlers;
    std::map< std::string, int > m_modules;
    std::unordered_set< std::string > m_loading;
    std::array< int, static_cast< size_t >( ScriptHandler::Count ) > m_handlerRefs;

    // LuauScheduler bookkeeping
    ScriptClock m_ownClock;
    ScriptClock* m_clock{ &m_ownClock };
    std::set< uint32_t > m_threadIds;     // threads whose ctx is this context
    std::set< uint32_t > m_changeWaiters; // threads parked in WaitKind::Change
    uint32_t m_syncThread{ 0 };           // reusable thread for LuauScheduler::syncThread, 0 = none
  };
}
