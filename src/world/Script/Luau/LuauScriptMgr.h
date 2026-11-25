#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Common.h>
#include <Config/ConfigDef.h>
#include <ForwardsZone.h>

#include "LuauBind.h"
#include "LuauContext.h"
#include "LuauRuntime.h"
#include "LuauScheduler.h"
#include "LuauSnapshot.h"

namespace Sapphire::Event
{
  struct SceneResult;
}

namespace Sapphire::Scripting
{
  // one handler argument; integers are passed as double( x ), 64-bit actor ids as uint64_t
  struct LuauArg
  {
    std::variant< std::monostate, double, bool, uint64_t, std::string, Common::Vector3, Entity::PlayerPtr,
                  Entity::BNpcPtr, Entity::EventObjectPtr, Entity::CharaPtr, World::Action::Action*,
                  StatusEffect::StatusEffect* > v;

    LuauArg() = default;
    LuauArg( double d ) : v( d ) {}
    LuauArg( bool b ) : v( b ) {}
    LuauArg( uint64_t id ) : v( id ) {}
    LuauArg( std::string s ) : v( std::move( s ) ) {}
    LuauArg( Common::Vector3 p ) : v( p ) {}
    LuauArg( Entity::PlayerPtr p ) : v( std::move( p ) ) {}
    LuauArg( Entity::BNpcPtr p ) : v( std::move( p ) ) {}
    LuauArg( Entity::EventObjectPtr p ) : v( std::move( p ) ) {}
    LuauArg( Entity::CharaPtr p ) : v( std::move( p ) ) {}
    LuauArg( World::Action::Action* a ) : v( a ) {}
    LuauArg( StatusEffect::StatusEffect* s ) : v( s ) {}

    void push( lua_State* L ) const;
  };

  struct ScriptEntry
  {
    ScriptKind kind;
    uint32_t id;
    std::filesystem::path path;
    std::string key; // normalised generic path; indexes LuauScriptMgr::m_byPath
    std::set< std::string > deps;
    bool retired = false; // file deleted; kept alive only by instance records
    std::unique_ptr< LuauContext > singleton;
  };

  struct ScriptScope
  {
    std::weak_ptr< Territory > territory;
    std::weak_ptr< Event::Director > director;   // empty when the scope has no director
    std::weak_ptr< InstanceContent > instance;   // empty unless the territory is instance content
    std::weak_ptr< QuestBattle > questBattle;    // empty unless the territory is a quest battle
    bool hasDirector = false;
    bool hasInstance = false;
    bool hasQuestBattle = false;
    std::function< void( uint8_t index, uint8_t value ) > setVar;
    std::function< void( uint8_t ) > setSequence;
    std::function< void( uint8_t ) > setFlags;
  };

  // what encounter.status( name ) reports for a started encounter; Success is final
  enum class ScriptEncounterState : uint8_t
  {
    Idle,
    Active,
    Success
  };

  const char* scriptEncounterStateName( ScriptEncounterState state ); // "idle" | "active" | "success"

  enum class VmRestart : uint8_t
  {
    Disabled,  // Luau scripting is off
    Deferred,  // a script segment is running: the next update() restarts
    Failed,    // the VM did not come back up (common.luau)
    Restarted
  };

  struct InstanceRecord
  {
    ScriptKind kind = ScriptKind::Instance;
    ScriptScope scope;
    uint32_t guid = 0;
    uint32_t directorId = 0;
    bool created = false;
    bool dutyCompleted = false;
    bool faulted = false;
    std::unordered_map< std::string, ScriptEncounterState > encounters; // by def.name
    std::string currentEncounter;
    ScriptEncounterState currentState = ScriptEncounterState::Idle; // encounters[ currentEncounter ]
    std::vector< uint32_t > bossLayoutIds;
    EncounterPtr encounter;
    int dataRef = -1;
    SavedValue savedData;
    std::shared_ptr< ScriptEntry > entry;
    std::unique_ptr< LuauContext > ctx;
  };

  class LuauScriptMgr : public ISchedulerHost
  {
  public:
    using NativeLookup = std::function< bool( ScriptKind, uint32_t id ) >;
    using ArgList = std::initializer_list< LuauArg >;

    LuauScriptMgr();
    ~LuauScriptMgr() override;

    // the live manager (one per process), nullptr outside its lifetime
    static LuauScriptMgr* instance();

    void init( const Common::Config::WorldConfig::Scripts& config, NativeLookup hasNative );
    void shutdown();
    void update( uint64_t nowMs );

    // The Luau script registered for an id, nullptr when there is none (the native script, if any, applies). The
    // hooks take it, so a dispatch looks the script up once. Valid until the next reload or update().
    ScriptEntry* script( ScriptKind kind, uint32_t id ) const;
    // by quest event id
    ScriptEntry* questScript( uint32_t eventId ) const;
    // instances and quest battles stay with the implementation they were created under
    bool ownsInstance( const InstanceContent& instance ) const;
    bool ownsBattle( const QuestBattle& battle ) const;

    // dispatch; the hooks return whether the handler exists and was started
    bool call( LuauContext& ctx, ScriptHandler handler, Entity::Player* owner, uint32_t eventId,
               const std::function< int( lua_State* ) >& pushArgs );
    bool eventHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId, ArgList extra );
    bool questHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId,
                    bool requireActive, ArgList extra );
    bool syncHook( ScriptEntry& script, ScriptHandler handler, ArgList args );
    bool zoneHook( ScriptEntry& script, ScriptHandler handler, Entity::Player& player, uint32_t eventId, ArgList extra );
    bool eobjHook( ScriptEntry& script, Entity::Player& player, Entity::EventObjectPtr eobj, uint32_t eventId );
    uint8_t getQuestAvailability( ScriptEntry& script, Entity::Player& player, uint32_t questId );

    // instance content
    bool onInstanceInit( InstanceContent& instance );
    void onInstanceDestroy( InstanceContent& instance );
    void onInstanceUpdate( InstanceContent& instance, uint64_t tickCount );
    void onInstanceReset( InstanceContent& instance );
    void onInstanceStateChange( InstanceContent& instance, uint32_t oldState, uint32_t newState );
    void onInstanceEnterTerritory( InstanceContent& instance, Entity::Player& player, uint32_t eventId, uint16_t param1,
                                   uint16_t param2 );
    void onInstanceLeaveTerritory( InstanceContent& instance, Entity::Player& player );
    void onInstanceDirectorVarChange( InstanceContent& instance, uint8_t var, uint8_t val );
    void onInstanceDirectorSeqChange( InstanceContent& instance, uint8_t seq );
    void onInstanceDirectorFlagChange( InstanceContent& instance, uint8_t flags );
    void onInstanceCustomVarChange( InstanceContent& instance );
    void onInstanceActorDeath( InstanceContent& instance, Entity::Chara& chara );
    bool onInstanceTalk( InstanceContent& instance, Entity::Player& player, Entity::EventObjectPtr eobj, uint32_t eventId,
                         uint64_t actorId );

    // quest battles
    bool onBattleInit( QuestBattle& battle );
    void onBattleDestroy( QuestBattle& battle );
    // director var / sequence / flags / custom var change or actor death: wakes the battle's wait.till predicates
    void onBattleChange( QuestBattle& battle );
    void onBattleUpdate( QuestBattle& battle, uint64_t tickCount );
    void onPlayerSetup( QuestBattle& battle, Entity::Player& player );
    void onDutyCommence( QuestBattle& battle, Entity::Player& player );
    void onBattleEnterTerritory( QuestBattle& battle, Entity::Player& player, uint32_t eventId, uint16_t param1,
                                 uint16_t param2 );
    void onDutyComplete( QuestBattle& battle, Entity::Player& player );

    // scenes and players
    bool onYield( Entity::Player& player, uint32_t eventId, uint16_t sceneId, uint8_t resumeId,
                  const std::string& resultString, uint64_t resultInt );
    void onPlayerLogout( Entity::Player& player );
    void onPlayerTerritoryChanged( Entity::Player& player );

    // ISchedulerHost
    void onYield( ScriptThread& thread ) override;
    void onExit( ScriptThread& thread, ExitReason reason ) override;
    void reportError( ScriptThread& thread, const std::string& message ) override;

    // used by the bindings
    bool running() const;
    LuauRuntime& runtime();
    LuauScheduler& scheduler();
    // the record of the running task's context, nullptr outside instance and quest battle scripts
    InstanceRecord* currentRecord();
    void notifyChange( InstanceRecord& record );
    // hook-scoped handles (Action, ActionResults, StatusEffect) are valid while their hook serial is live
    uint64_t beginHook();
    void endHook( uint64_t serial );
    bool isHookLive( uint64_t serial ) const;
    uint64_t currentHookSerial() const;
    void flushQuests( ScriptThread& thread );
    // eventAction waits: each wait gets a token; callbacks of an older wait on the same thread are ignored
    uint64_t beginActionWait( uint32_t threadId );
    // eventAction callbacks: the waiting thread is resumed from the next update() if token is still current
    void deferActionResume( uint32_t threadId, uint64_t token, bool finished );

    // scenes (bindings of Player:playScene)
    uint64_t sceneBegin( lua_State* L, Entity::Player& player, uint32_t eventId, uint32_t scene, uint32_t flags,
                         const std::vector< uint32_t >& params );
    int sceneAwait( lua_State* L, uint64_t token );
    void sceneResume( lua_State* L, uint64_t token, uint8_t yieldId, const std::vector< uint32_t >& values );

    // debug commands
    std::vector< std::string > statusLines();
    std::vector< std::string > reloadByName( const std::string& name );
    VmRestart restartVm();
    bool writeTypes( std::string& outPath );
    std::vector< std::string > dumpLines( uint32_t guid );

  private:
    struct SceneEvent
    {
      bool isReturn = false;
      uint8_t errorCode = 0;
      std::vector< uint32_t > results;
      std::string string;
      uint64_t intResult = 0;
      uint8_t yieldId = 0;
    };

    struct PendingScene
    {
      uint64_t token;
      uint32_t threadId;
      uint32_t playerId;
      uint32_t eventId;
      uint32_t sceneId;
      std::weak_ptr< Entity::Player > player;
      std::deque< SceneEvent > queue;
    };

    using RegistryKey = std::pair< ScriptKind, uint32_t >;

    bool startVm();
    void stopVm();
    void scan();
    void scanDir( const std::filesystem::path& dir );
    std::unique_ptr< LuauContext > discover( const std::filesystem::path& file );
    std::shared_ptr< ScriptEntry > registerScript( std::unique_ptr< LuauContext > ctx, bool announce );
    void unregisterScript( const std::shared_ptr< ScriptEntry >& entry );
    void reloadScript( const std::shared_ptr< ScriptEntry >& entry );
    bool buildRecordContext( InstanceRecord& record, bool runCreate );
    void destroyRecord( uint32_t guid );
    void warnUnknownHandlers( LuauContext& ctx );
    void logDropped( uint32_t guid, const std::vector< std::string >& dropped, const char* moment );
    void pollFiles( uint64_t nowMs );
    void cleanupEvents( std::vector< OwnedEvent >& events );
    void driveEncounter( InstanceRecord& record, uint64_t tickCount );
    void onSceneReturn( std::pair< uint32_t, uint32_t > key, uint64_t token, const Event::SceneResult& result,
                        World::Quest* quest );
    void pushSceneEvent( lua_State* L, const SceneEvent& ev );

    const std::shared_ptr< ScriptEntry >& findEntry( ScriptKind kind, uint32_t id ) const;
    InstanceRecord* findRecord( uint32_t guid );
    bool recordHook( uint32_t guid, ScriptHandler handler, Entity::Player* owner, uint32_t eventId, ArgList args );
    // starts a handler thread: fnRef is the function's registry ref, pushArgs pushes the arguments
    bool spawnHandler( LuauContext& ctx, int fnRef, Entity::Player* owner, uint32_t eventId,
                       const std::function< int( lua_State* ) >& pushArgs );
    InstanceRecord& createRecord( ScriptKind kind, Territory& territory, const std::shared_ptr< ScriptEntry >& entry );
    std::shared_ptr< ScriptEntry > entryByPath( const std::string& path ) const;
    void buildPolicies();
    bool isScriptDir( const std::filesystem::path& file ) const;

    Common::Config::WorldConfig::Scripts m_config{};
    NativeLookup m_hasNative;
    std::filesystem::path m_root;
    bool m_enabled{ false };

    LuauRuntime m_runtime;
    std::unique_ptr< LuauScheduler > m_scheduler;
    Bind::Registry m_bindings;
    bool m_bindingsBuilt{ false };
    std::array< ScriptPolicy, static_cast< size_t >( ScriptKind::Count ) > m_policies{};

    std::map< RegistryKey, std::shared_ptr< ScriptEntry > > m_registry;
    std::unordered_map< std::string, std::shared_ptr< ScriptEntry > > m_byPath; // ScriptEntry::key -> live entry
    std::map< uint32_t, std::unique_ptr< InstanceRecord > > m_records;
    std::map< std::pair< uint32_t, uint32_t >, PendingScene > m_scenes;
    uint64_t m_nextSceneToken{ 1 };
    std::vector< std::vector< OwnedEvent > > m_deferredCleanup;
    struct DeferredAction
    {
      uint32_t threadId;
      uint64_t token;
      bool finished;
    };
    // eventAction continuations, resumed from update() outside Player::processActions
    std::vector< DeferredAction > m_deferredActions;
    std::unordered_map< uint32_t, uint64_t > m_actionWaits; // threadId -> token of its current eventAction wait
    uint64_t m_nextActionToken{ 1 };
    // VM restarts / reloads requested while a script segment was running; carried out by the next update()
    bool m_restartPending{ false };
    std::vector< std::weak_ptr< ScriptEntry > > m_pendingReloads;
    uint64_t m_nextHookSerial{ 1 };
    std::vector< uint64_t > m_liveHooks;
    uint64_t m_errorCount{ 0 };

    struct PendingFile
    {
      std::filesystem::file_time_type mtime;
      uint64_t firstSeenMs;
    };
    std::map< std::string, std::filesystem::file_time_type > m_fileTimes;
    std::map< std::string, PendingFile > m_pendingFiles;
    // scripts whose discovery failed: path -> dependencies captured before the failure; retried when one changes
    std::map< std::string, std::set< std::string > > m_failedScripts;
    bool m_filesScanned{ false };
    uint64_t m_lastPollMs{ 0 };
  };
}
