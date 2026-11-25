#include <Logging/Logger.h>
#include <Exd/ExdData.h>

#include <watchdog/Watchdog.h>
#include <Service.h>

#include "Territory/Territory.h"
#include "Territory/InstanceContent.h"
#include "Territory/QuestBattle.h"
#include "Actor/Player.h"
#include "Actor/EventObject.h"
#include "Actor/BNpc.h"
#include "WorldServer.h"
#include "Event/EventHandler.h"
#include "Action/Action.h"

#include "Manager/EventMgr.h"
#include "Manager/PlayerMgr.h"
#include "Manager/TerritoryMgr.h"

#include "StatusEffect/StatusEffect.h"

#include "Network/PacketWrappers/ServerNoticePacket.h"

#include "Script/ScriptMgr.h"
#include "Script/Luau/LuauScriptMgr.h"

#include "NativeScriptMgr.h"
#include "WorldServer.h"

#include "Quest/Quest.h"

using namespace Sapphire::World::Manager;
// enable the ambiguity fix for every platform to avoid #define nonsense
#define WIN_AMBIGUITY_FIX

namespace fs = std::filesystem;

Sapphire::Scripting::ScriptMgr::ScriptMgr() :
  ScriptMgr( createNativeScriptMgr( Common::Service< World::WorldServer >::ref().getConfig().scripts.cachePath ) )
{
}

Sapphire::Scripting::ScriptMgr::ScriptMgr( std::shared_ptr< NativeScriptMgr > nativeScriptMgr ) :
  m_nativeScriptMgr( std::move( nativeScriptMgr ) ),
  m_luau( std::make_unique< LuauScriptMgr >() ),
  m_firstScriptChangeNotificiation( false )
{
}

Sapphire::Scripting::ScriptMgr::~ScriptMgr()
{
  stopWatchingDirectories();
}

void Sapphire::Scripting::ScriptMgr::stopWatchingDirectories()
{
  if( !m_watchingDirectories )
    return;

  Watchdog::unwatchAll();
  m_watchingDirectories = false;
}

void Sapphire::Scripting::ScriptMgr::shutdown()
{
  m_luau->shutdown();

  // Stop watching for changes and unload scripts
  stopWatchingDirectories();
  if( m_nativeScriptMgr )
  {
    m_nativeScriptMgr->unloadAll();
  }
  // Reset flag so next init ignores the first Watchdog burst again
  m_firstScriptChangeNotificiation = false;
}

void Sapphire::Scripting::ScriptMgr::update()
{
  m_nativeScriptMgr->processLoadQueue();
  m_luau->update( Common::Util::getTimeMs() );
}

bool Sapphire::Scripting::ScriptMgr::init()
{
  std::set< std::string > files;
  auto& server = Common::Service< World::WorldServer >::ref();

  auto status = loadDir( server.getConfig().scripts.path, files, m_nativeScriptMgr->getModuleExtension() );

  if( !status )
  {
    Logger::error(
      "ScriptMgr: failed to load modules, the server will not function correctly without scripts loaded." );
    return false;
  }

  uint32_t scriptsFound = 0;
  uint32_t scriptsLoaded = 0;

  for( auto itr = files.begin(); itr != files.end(); ++itr )
  {
    auto& path = *itr;

    scriptsFound++;

    if( m_nativeScriptMgr->loadScript( path ) )
      scriptsLoaded++;
  }

  Logger::info( "ScriptMgr: Loaded {0}/{1} modules", scriptsLoaded, scriptsFound );

  m_luau->init( server.getConfig().scripts, [ this ]( ScriptKind kind, uint32_t id )
  {
    switch( kind )
    {
      case ScriptKind::Instance:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( id ) != nullptr;
      case ScriptKind::Quest:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( id ) != nullptr;
      case ScriptKind::Event:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( id ) != nullptr;
      case ScriptKind::QuestBattle:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( id ) != nullptr;
      case ScriptKind::Action:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( id ) != nullptr;
      case ScriptKind::Status:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::StatusEffectScript >( id ) != nullptr;
      case ScriptKind::Zone:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ZoneScript >( id ) != nullptr;
      case ScriptKind::EObj:
        return m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventObjectScript >( id ) != nullptr;
      default:
        return false;
    }
  } );

  watchDirectories();

  return true;
}

void Sapphire::Scripting::ScriptMgr::watchDirectories()
{
  auto& server = Common::Service< World::WorldServer >::ref();
  auto shouldWatch = server.getConfig().scripts.hotSwap;
  if( !shouldWatch )
    return;

  Watchdog::watchMany( server.getConfig().scripts.path + "*" +
                       m_nativeScriptMgr->getModuleExtension(),
                       [ this ]( const std::vector< ci::fs::path >& paths )
                       {
                         if( !m_firstScriptChangeNotificiation )
                         {
                           // for whatever reason, the first time this runs, it detects every file as changed
                           // so we're always going to ignore the first notification
                           m_firstScriptChangeNotificiation = true;
                           return;
                         }

                         for( const auto& path : paths )
                         {
                           if( m_nativeScriptMgr->isModuleLoaded( path.stem().string() ) )
                           {
                             Logger::debug( "Reloading changed script: {0}", path.stem().string() );

                             m_nativeScriptMgr->queueScriptReload( path.stem().string() );
                           }
                           else
                           {
                             Logger::debug( "Loading new script: {0}", path.stem().string() );

                             m_nativeScriptMgr->loadScript( path.string() );
                           }
                         }
                       } );
  m_watchingDirectories = true;
}

bool Sapphire::Scripting::ScriptMgr::loadDir( const std::string& dirname, std::set< std::string >& files,
                                              const std::string& ext )
{
  Logger::info( "ScriptMgr: loading scripts from: {0}", dirname );

  if( !fs::exists( dirname ) )
  {
    Logger::error( "ScriptMgr: scripts directory doesn't exist" );
    return false;
  }

  fs::path targetDir( dirname );

  fs::directory_iterator iter( targetDir );

  for( const auto& i : iter )
  {
    if( fs::is_regular_file( i ) && fs::path( i ).extension() == ext )
    {
      files.insert( fs::path( i ).string() );
    }
  }

  if( !files.empty() )
    return true;
  else
  {
    Logger::error( "ScriptMgr: couldn't find any script modules" );
    return false;
  }
}

void Sapphire::Scripting::ScriptMgr::onPlayerFirstEnterWorld( Entity::Player& player )
{
  //   try
  //   {
  //      std::string test = m_onFirstEnterWorld( player );
  //   }
  //   catch( const std::exception &e )
  //   {
  //      std::string what = e.what();
  //   }
}

bool Sapphire::Scripting::ScriptMgr::onTalk( Entity::Player& player, uint64_t actorId, uint32_t eventId )
{
  auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& exdData = Common::Service< Data::ExdData >::ref();
  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      auto actorBaseId = Common::Service< World::Manager::EventMgr >::ref().getActorBaseId( static_cast< uint32_t >( actorId ) );
      m_luau->questHook( *luau, ScriptHandler::onTalk, player, eventId, false, { double( actorBaseId ) } );
      return true;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
    {
      auto questInfo = exdData.getRow< Excel::Quest >( eventId );
      if( questInfo )
      {
        World::Manager::PlayerMgr::sendUrgent( player, "Quest not implemented: {0} ({1})",
                                               questInfo->getString( questInfo->data().Text.Name ), eventId );
        return false;
      }
    }

    auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();
    auto actor = pEventMgr.getActorBaseId( static_cast< uint32_t >( actorId ) );

    auto questId = static_cast< uint16_t >( eventId );
    if( player.hasQuest( eventId ) )
    {
      World::Quest preQ;
      auto questIdx = player.getQuestIndex( questId );
      auto& quest = player.getQuestByIndex( questIdx );
      preQ = quest;
      script->onTalk( quest, player, actor );
      if( quest != preQ )
        player.updateQuest( quest );
    }
    else
    {
      auto newQuest = World::Quest( questId, 0, 0 );
      script->onTalk( newQuest, player, actor );
    }
    return true;
  }

  // all other types....

  // check if the actor is an eobj and call its script if we have one
  auto& teriMgr = Common::Service< World::Manager::TerritoryMgr >::ref();
  auto zone = teriMgr.getTerritoryByGuId( player.getTerritoryId() );
  // an instance created under a Luau script stays Luau for its lifetime
  auto luauInstance = zone ? zone->getAsInstanceContent() : nullptr;
  if( luauInstance && !m_luau->ownsInstance( *luauInstance ) )
    luauInstance = nullptr;

  if( auto eobj = zone ? zone->getEObj( static_cast< uint32_t >( actorId ) ) : nullptr )
  {
    if( auto* luau = m_luau->script( ScriptKind::EObj, eobj->getBaseId() ) )
    {
      m_luau->eobjHook( *luau, player, eobj, eventId );
      return true;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventObjectScript >( eobj->getBaseId() );
    if( script )
    {
      script->onTalk( eventId, player, *eobj );
      return true;
    }

    if( luauInstance )
    {
      if( m_luau->onInstanceTalk( *luauInstance, player, eobj, eventId, actorId ) )
        return true;
    }
    else if( auto instance = zone->getAsInstanceContent() )
    {
      auto instanceScript = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >(
        instance->getDirectorId() );
      if( instanceScript )
      {
        instanceScript->onTalk( *instance, player, *eobj, eventId );
        return true;
      }
    }
  }

  // check for a direct eventid match first, otherwise default to base type
  if( auto* luau = m_luau->script( ScriptKind::Event, eventId ) )
  {
    m_luau->eventHook( *luau, ScriptHandler::onTalk, player, eventId, { actorId } );
    return true;
  }
  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
  if( script )
  {
    script->onTalk( eventId, player, actorId );
    return true;
  }
  if( auto* luau = m_luau->script( ScriptKind::Event, eventId & 0xFFFF0000 ) )
  {
    m_luau->eventHook( *luau, ScriptHandler::onTalk, player, eventId, { actorId } );
    return true;
  }
  script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId & 0xFFFF0000 );
  if( script )
  {
    script->onTalk( eventId, player, actorId );
    return true;
  }
  // fallthrough to instance content check
  // todo: this zone check might fumbling it. need testing
  if( luauInstance )
    return m_luau->onInstanceTalk( *luauInstance, player, nullptr, eventId, actorId );

  if( auto instance = zone ? zone->getAsInstanceContent() : nullptr )
  {
    auto instanceScript = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance->getDirectorId() );
    if( instanceScript )
    {
      instanceScript->onTalk( *instance, player, eventId, actorId );
      return true;
    }
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onYield( Entity::Player& player, uint32_t eventId, uint16_t sceneId,
                                              uint8_t resumeId,
                                              const std::string& resultString, uint64_t resultInt )
{
  if( m_luau->onYield( player, eventId, sceneId, resumeId, resultString, resultInt ) )
    return true;

  auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& exdData = Common::Service< Data::ExdData >::ref();
  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    Logger::error( "Yield unimplemented for EventHandlerType::Quest!" );
    return false;
  }

  // check for a direct eventid match first, otherwise default to base type
  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
  if( script )
  {
    script->onYield( eventId, sceneId, resumeId, player, resultString, resultInt );
    return true;
  }
  else
  {
    script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId & 0xFFFF0000 );
    if( !script )
      return false;

    script->onYield( eventId, sceneId, resumeId, player, resultString, resultInt );
    return true;
  }
}

bool Sapphire::Scripting::ScriptMgr::onEnterTerritory( Entity::Player& player, uint32_t eventId, uint16_t param1,
                                                       uint16_t param2 )
{
  const auto eventType = static_cast< uint16_t >( eventId >> 16 );

  if( auto* luau = eventType != Event::EventHandler::EventHandlerType::Quest ? m_luau->script( ScriptKind::Event, eventId ) : nullptr )
  {
    m_luau->eventHook( *luau, ScriptHandler::onEnterTerritory, player, eventId, { double( param1 ), double( param2 ) } );
    return true;
  }

  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onEnterTerritory, player, eventId, true, { double( param1 ), double( param2 ) } );
      return true;
    }
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
      return false;
    if( player.hasQuest( eventId ) )
    {
      auto idx = player.getQuestIndex( eventId );
      auto& quest = player.getQuestByIndex( idx );
      script->onEnterTerritory( quest, player, param1, param2 );
    }
  }
  else
  {
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
    if( !script )
      return false;
    script->onEnterTerritory( player, eventId, param1, param2 );
  }

  return true;
}

bool Sapphire::Scripting::ScriptMgr::onWithinRange( Entity::Player& player, uint32_t eventId, uint32_t param1, float x,
                                                    float y, float z )
{
  const auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();

  if( auto* luau = eventType != Event::EventHandler::EventHandlerType::Quest ? m_luau->script( ScriptKind::Event, eventId ) : nullptr )
  {
    m_luau->eventHook( *luau, ScriptHandler::onWithinRange, player, eventId, { double( param1 ), Common::Vector3{ x, y, z } } );
    return true;
  }

  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onWithinRange, player, eventId, true,
                         { double( eventId ), double( param1 ), Common::Vector3{ x, y, z } } );
      return true;
    }
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
      return false;
    if( player.hasQuest( eventId ) )
    {
      auto idx = player.getQuestIndex( eventId );
      auto& quest = player.getQuestByIndex( idx );
      World::Quest preQ = quest;
      script->onWithinRange( quest, player, eventId, param1, x, y, z );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
  else if( auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId ) )
  {
    script->onWithinRange( player, eventId, param1, x, y, z );
  }
  else if( auto* luau = m_luau->script( ScriptKind::Zone, player.getTerritoryTypeId() ) )
  {
    m_luau->zoneHook( *luau, ScriptHandler::onWithinRange, player, eventId,
                      { double( param1 ), Common::Vector3{ x, y, z } } );
  }
  else if( auto script = m_nativeScriptMgr->getScript<
    Sapphire::ScriptAPI::ZoneScript >( player.getTerritoryTypeId() ) )
  {
    script->onWithinRange( player, eventId, param1, x, y, z );
  }
  else
  {
    return false;
  }

  return true;
}

bool Sapphire::Scripting::ScriptMgr::onOutsideRange( Entity::Player& player, uint32_t eventId, uint32_t param1, float x,
                                                     float y, float z )
{
  const auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();

  if( auto* luau = eventType != Event::EventHandler::EventHandlerType::Quest ? m_luau->script( ScriptKind::Event, eventId ) : nullptr )
  {
    m_luau->eventHook( *luau, ScriptHandler::onOutsideRange, player, eventId, { double( param1 ), Common::Vector3{ x, y, z } } );
    return true;
  }

  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onOutsideRange, player, eventId, true,
                         { double( eventId ), double( param1 ), Common::Vector3{ x, y, z } } );
      return true;
    }
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
      return false;
    if( player.hasQuest( eventId ) )
    {
      auto idx = player.getQuestIndex( eventId );
      auto& quest = player.getQuestByIndex( idx );
      World::Quest preQ = quest;
      script->onOutsideRange( quest, player, eventId, param1, x, y, z );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
  else
  {
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
    if( !script )
      return false;
    script->onOutsideRange( player, eventId, param1, x, y, z );
  }

  return true;
}

bool Sapphire::Scripting::ScriptMgr::onEmote( Entity::Player& player, uint64_t actorId, uint32_t eventId,
                                              uint8_t emoteId )
{
  const auto eventType = static_cast< uint16_t >( eventId >> 16 );

  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();
  auto actor = pEventMgr.getActorBaseId( static_cast< uint32_t >( actorId ) );
  if( auto* luau = eventType != Event::EventHandler::EventHandlerType::Quest ? m_luau->script( ScriptKind::Event, eventId ) : nullptr )
  {
    m_luau->eventHook( *luau, ScriptHandler::onEmote, player, eventId, { double( actor ), double( emoteId ) } );
    return true;
  }

  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onEmote, player, eventId, true, { double( actor ), double( emoteId ) } );
      return true;
    }
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
      return false;
    if( player.hasQuest( eventId ) )
    {
      auto idx = player.getQuestIndex( eventId );
      auto& quest = player.getQuestByIndex( idx );
      World::Quest preQ = quest;
      script->onEmote( quest, actor, emoteId, player );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
  else
  {
    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
    if( !script )
      return false;
    script->onEmote( actor, eventId, emoteId, player );
  }

  return true;
}

bool Sapphire::Scripting::ScriptMgr::onEventHandlerReturn( Entity::Player& player, uint32_t eventId, uint16_t subEvent,
                                                           uint16_t param1, uint16_t param2,
                                                           uint16_t param3 )
{
  return false;
}

bool Sapphire::Scripting::ScriptMgr::onEventHandlerTradeReturn( Entity::Player& player, uint32_t eventId,
                                                                uint16_t subEvent, uint16_t param, uint32_t catalogId )
{
  if( auto* luau = m_luau->script( ScriptKind::Event, eventId ) )
  {
    m_luau->eventHook( *luau, ScriptHandler::onTradeReturn, player, eventId,
                       { double( subEvent ), double( param ), double( catalogId ) } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::EventScript >( eventId );
  if( script )
  {
    script->onEventHandlerTradeReturn( player, eventId, subEvent, param, catalogId );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onEventItem( Entity::Player& player, uint32_t eventItemId, uint32_t eventId,
                                                  uint64_t targetId )
{
  auto& eventMgr = Common::Service< World::Manager::EventMgr >::ref();
  std::string eventName = "onEventItem";
  std::string objName = eventMgr.getEventName( eventId );
  PlayerMgr::sendDebug( player, "Calling: {0}.{1} - {2}", objName, eventName, eventId );

  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();
  auto actor = pEventMgr.getActorBaseId( static_cast< uint32_t >( targetId ) );

  auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& exdData = Common::Service< Data::ExdData >::ref();
  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
      return m_luau->questHook( *luau, ScriptHandler::onEventItem, player, eventId, true, { double( actor ) } );

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( script )
    {
      auto questId = static_cast< uint16_t >( eventId );
      if( player.hasQuest( eventId ) )
      {
        World::Quest preQ;
        auto questIdx = player.getQuestIndex( questId );
        auto& quest = player.getQuestByIndex( questIdx );
        preQ = quest;
        script->onEventItem( quest, player, actor );
        if( quest != preQ )
          player.updateQuest( quest );
        return true;
      }
    }
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onEventGroundItem( Entity::Player& player, uint32_t eventItemId, uint32_t eventId,
                                                        Common::Vector3 pos )
{
  auto& eventMgr = Common::Service< World::Manager::EventMgr >::ref();
  std::string eventName = "onEventGroundItem";
  std::string objName = eventMgr.getEventName( eventId );
  PlayerMgr::sendDebug( player, "Calling: {0}.{1} - {2}", objName, eventName, eventId );

  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();

  auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& exdData = Common::Service< Data::ExdData >::ref();
  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
      return m_luau->questHook( *luau, ScriptHandler::onEventGroundItem, player, eventId, true, { pos } );

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( script )
    {
      auto questId = static_cast< uint16_t >( eventId );
      if( player.hasQuest( eventId ) )
      {
        World::Quest preQ;
        auto questIdx = player.getQuestIndex( questId );
        auto& quest = player.getQuestByIndex( questIdx );
        preQ = quest;
        script->onEventGroundItem( quest, player, pos );
        if( quest != preQ )
          player.updateQuest( quest );
        return true;
      }
    }
  }

  return false;
}

void Sapphire::Scripting::ScriptMgr::onTriggerOwnerDeaggro( Entity::Player& player, Entity::BNpc& bnpc )
{
  auto& eventMgr = Common::Service< World::Manager::EventMgr >::ref();

  // loop through all active quests and try to call available onBNpcKill callbacks
  for( size_t i = 0; i < 30; i++ )
  {
    auto quest = player.getQuestByIndex( static_cast< uint16_t >( i ) );
    if( quest.getId() == 0 )
      continue;

    uint32_t questId = quest.getId() | Event::EventHandler::EventHandlerType::Quest << 16;

    if( auto* luau = m_luau->questScript( questId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onTriggerOwnerDeaggro, player, questId, true, { bnpc.getAsBNpc() } );
      continue;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId );
    if( script )
    {
      std::string objName = eventMgr.getEventName( questId );

      PlayerMgr::sendDebug( player, "Calling: {0}.onTriggerOwnerDeaggro layoutId#{1}", objName, bnpc.getLayoutId() );

      World::Quest preQ = quest;
      script->onTriggerOwnerDeaggro( quest, bnpc, player );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
}

void Sapphire::Scripting::ScriptMgr::onBNpcKill( const World::Notifications::NpcKillCredited& event )
{
  auto& player = event.player;
  auto& bnpc = event.npc;
  auto& eventMgr = Common::Service< World::Manager::EventMgr >::ref();

  // loop through all active quests and try to call available onBNpcKill callbacks
  for( size_t i = 0; i < 30; i++ )
  {
    auto quest = player.getQuestByIndex( static_cast< uint16_t >( i ) );
    if( quest.getId() == 0 )
      continue;

    uint32_t questId = quest.getId() | Event::EventHandler::EventHandlerType::Quest << 16;

    if( auto* luau = m_luau->questScript( questId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onBNpcKill, player, questId, true, { bnpc.getAsBNpc() } );
      continue;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId );
    if( script )
    {
      std::string objName = eventMgr.getEventName( questId );

      PlayerMgr::sendDebug( player, "Calling: {0}.onBnpcKill nameId#{1}", objName, bnpc.getBNpcNameId() );


      World::Quest preQ = quest;
      script->onBNpcKill( quest, bnpc, player );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
}

void Sapphire::Scripting::ScriptMgr::onPlayerDeath( const World::Notifications::PlayerDied& event )
{
  auto& player = event.player;

  // loop through all active quests and try to call available onPlayerDeath callbacks
  for( size_t i = 0; i < 30; i++ )
  {
    auto quest = player.getQuestByIndex( static_cast< uint16_t >( i ) );
    if( quest.getId() == 0 )
      continue;

    uint32_t questId = quest.getId() | Event::EventHandler::EventHandlerType::Quest << 16;

    if( auto* luau = m_luau->questScript( questId ) )
    {
      m_luau->questHook( *luau, ScriptHandler::onPlayerDeath, player, questId, true, {} );
      continue;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId );
    if( script )
    {
      PlayerMgr::sendDebug( player, "Calling quest {0}.onPlayerDeath for {1}", questId, player.getName() );

      World::Quest preQ = quest;
      script->onPlayerDeath( quest, player );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }
}

bool Sapphire::Scripting::ScriptMgr::onEObjHit( Sapphire::Entity::Player& player, uint64_t actorId, uint32_t actionId )
{
  auto& eventMgr = Common::Service< World::Manager::EventMgr >::ref();
  bool didCallScript = false;

  auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();
  auto actor = pEventMgr.getActorBaseId( static_cast< uint32_t >( actorId ) );

  for( size_t i = 0; i < 30; i++ )
  {
    auto quest = player.getQuestByIndex( static_cast< uint16_t >( i ) );
    if( quest.getId() == 0 )
      continue;

    uint32_t questId = quest.getId() | Event::EventHandler::EventHandlerType::Quest << 16;

    if( auto* luau = m_luau->questScript( questId ) )
    {
      didCallScript = true;
      m_luau->questHook( *luau, ScriptHandler::onEObjHit, player, questId, true, { double( actor ), double( actionId ) } );
      continue;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId );
    if( script )
    {
      didCallScript = true;
      std::string objName = eventMgr.getEventName( questId );

      PlayerMgr::sendDebug( player, "Calling: {0}.onEObjHit actorId#{1}, questId#{2}", objName, actor, quest.getId() );

      World::Quest preQ = quest;
      script->onEObjHit( quest, player, actor, actionId );
      if( quest != preQ )
        player.updateQuest( quest );
    }
  }

  return didCallScript;
}

bool Sapphire::Scripting::ScriptMgr::onExecute( World::Action::Action& action )
{
  if( auto* luau = m_luau->script( ScriptKind::Action, action.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onExecute, { &action } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( action.getId() );

  if( script )
  {
    script->onExecute( action );
    return true;
  }
  return false;
}

bool Sapphire::Scripting::ScriptMgr::onAfterBuildEffect( World::Action::Action& action )
{
  if( auto* luau = m_luau->script( ScriptKind::Action, action.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onAfterBuildEffect, { &action } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( action.getId() );

  if( script )
  {
    script->onAfterBuildEffect( action );
    return true;
  }
  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInterrupt( World::Action::Action& action )
{
  if( auto* luau = m_luau->script( ScriptKind::Action, action.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onInterrupt, { &action } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( action.getId() );

  if( script )
  {
    script->onInterrupt( action );
    return true;
  }
  return false;
}

bool Sapphire::Scripting::ScriptMgr::onBeforeBootstrap( World::Action::Action& action )
{
  if( !m_nativeScriptMgr )
  {
    // Defensive: scripts system not initialized yet
    return false;
  }

  if( auto* luau = m_luau->script( ScriptKind::Action, action.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onBeforeBootstrap, { &action } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( action.getId() );

  if( script )
  {
    script->onBeforeBootstrap( action );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onStart( World::Action::Action& action )
{
  if( auto* luau = m_luau->script( ScriptKind::Action, action.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onStart, { &action } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ActionScript >( action.getId() );

  if( script )
  {
    script->onStart( action );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onStatusReceive( Entity::CharaPtr pActor, uint32_t effectId )
{
  if( auto* luau = m_luau->script( ScriptKind::Status, effectId ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onApply, { pActor } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::StatusEffectScript >( effectId );

  if( script )
  {
    if( pActor->isPlayer() )
      PlayerMgr::sendDebug( *pActor->getAsPlayer(), "Calling status receive for statusid#{0}", effectId );

    script->onApply( *pActor );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onStatusTick( Entity::CharaPtr pActor,
                                                   Sapphire::StatusEffect::StatusEffect& effect )
{
  if( auto* luau = m_luau->script( ScriptKind::Status, effect.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onTick, { pActor, &effect } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::StatusEffectScript >( effect.getId() );
  if( script )
  {
    script->onTick( *pActor, effect );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onStatusTimeOut( Entity::CharaPtr pChara, uint32_t effectId )
{
  if( auto* luau = m_luau->script( ScriptKind::Status, effectId ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onExpire, { pChara } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::StatusEffectScript >( effectId );
  if( script )
  {
    if( pChara->isPlayer() )
      PlayerMgr::sendDebug( *pChara->getAsPlayer(), "Calling status timeout for statusid#{0}", effectId );

    script->onExpire( *pChara );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onPlayerHit( Entity::CharaPtr& pHitChara,
                                                  Sapphire::StatusEffect::StatusEffect& effect )
{
  if( auto* luau = m_luau->script( ScriptKind::Status, effect.getId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onPlayerHit, { pHitChara, &effect } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::StatusEffectScript >( effect.getId() );
  if( script )
  {
    script->onPlayerHit( *pHitChara, effect );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onZoneInit( const Territory& zone )
{
  if( auto* luau = m_luau->script( ScriptKind::Zone, zone.getTerritoryTypeId() ) )
  {
    m_luau->syncHook( *luau, ScriptHandler::onZoneInit, { double( zone.getTerritoryTypeId() ) } );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::ZoneScript >( zone.getTerritoryTypeId() );
  if( script )
  {
    script->onZoneInit();
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceInit( InstanceContent& instance )
{
  if( m_luau->onInstanceInit( instance ) )
    return true;

  auto instId = instance.getDirectorId();
  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );

  if( script )
  {
    script->onInit( instance );

    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceReset( InstanceContent& instance )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceReset( instance );
    return true;
  }

  auto instId = instance.getDirectorId();
  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );

  if( script )
  {
    script->onReset( instance );

    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceUpdate( InstanceContent& instance, uint64_t tickCount )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceUpdate( instance, tickCount );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );

  if( script )
  {
    script->onUpdate( instance, tickCount );

    return true;
  }

  return false;
}


bool Sapphire::Scripting::ScriptMgr::onInstanceStateChange( Sapphire::InstanceContent& instance,
                                                            Sapphire::InstanceContentState oldState,
                                                            Sapphire::InstanceContentState newState )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceStateChange( instance, static_cast< uint32_t >( oldState ), static_cast< uint32_t >( newState ) );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );

  if( script )
  {
    script->onStateChange( instance, oldState, newState );

    return true;
  }

  return false;
}


bool Sapphire::Scripting::ScriptMgr::onInstanceEnterTerritory( InstanceContent& instance, Entity::Player& player,
                                                               uint32_t eventId, uint16_t param1, uint16_t param2 )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceEnterTerritory( instance, player, eventId, param1, param2 );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onEnterTerritory( instance, player, eventId, param1, param2 );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceLeaveTerritory( InstanceContent& instance, Entity::Player& player )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceLeaveTerritory( instance, player );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onLeaveTerritory( instance, player );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceDirectorSeqChange( InstanceContent& instance, uint8_t seq )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceDirectorSeqChange( instance, seq );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onDirectorSeqChange( instance, seq );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceDirectorFlagChange( InstanceContent& instance, uint8_t flag )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceDirectorFlagChange( instance, flag );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onDirectorFlagChange( instance, flag );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceDirectorVarChange( InstanceContent& instance, uint8_t var, uint8_t val )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceDirectorVarChange( instance, var, val );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onDirectorVarChange( instance, var, val );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceCustomVarChange( InstanceContent& instance, uint32_t var, uint64_t val )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceCustomVarChange( instance );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onCustomVarChange( instance, var, val );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceActorDeath( InstanceContent& instance, Entity::Chara& chara )
{
  if( m_luau->ownsInstance( instance ) )
  {
    m_luau->onInstanceActorDeath( instance, chara );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::InstanceContentScript >( instance.getDirectorId() );
  if( script )
  {
    script->onActorDeath( instance, chara );
    return true;
  }

  return false;
}

void Sapphire::Scripting::ScriptMgr::onInstanceDestroy( InstanceContent& instance )
{
  m_luau->onInstanceDestroy( instance );
}

void Sapphire::Scripting::ScriptMgr::onInstanceDestroy( QuestBattle& instance )
{
  m_luau->onBattleDestroy( instance );
}

void Sapphire::Scripting::ScriptMgr::onPlayerLogout( const World::Notifications::PlayerLoggedOut& event )
{
  m_luau->onPlayerLogout( event.player );
}

void Sapphire::Scripting::ScriptMgr::onPlayerTerritoryChanged( const World::Notifications::PlayerTerritoryChanged& event )
{
  m_luau->onPlayerTerritoryChanged( event.player );
}

bool Sapphire::Scripting::ScriptMgr::hasQuestScript( uint32_t questId )
{
  return m_luau->questScript( questId ) != nullptr ||
         m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId ) != nullptr;
}

uint8_t Sapphire::Scripting::ScriptMgr::getQuestAvailability( Entity::Player& player, uint32_t questId )
{
  if( auto* luau = m_luau->questScript( questId ) )
    return m_luau->getQuestAvailability( *luau, player, questId );
  if( auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( questId ) )
    return static_cast< uint8_t >( script->getQuestAvailability( player, questId ) );
  return static_cast< uint8_t >( Event::EventHandler::QuestAvailability::Available );
}

Sapphire::Scripting::LuauScriptMgr& Sapphire::Scripting::ScriptMgr::getLuauScriptHandler()
{
  return *m_luau;
}

bool Sapphire::Scripting::ScriptMgr::onPlayerSetup( QuestBattle& instance, Entity::Player& player )
{
  if( m_luau->ownsBattle( instance ) )
  {
    m_luau->onPlayerSetup( instance, player );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );
  if( script )
  {
    script->onPlayerSetup( instance, player );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceInit( QuestBattle& instance )
{
  if( m_luau->onBattleInit( instance ) )
    return true;

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );
  if( script )
  {
    script->onInit( instance );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceUpdate( QuestBattle& instance, uint64_t tickCount )
{
  if( m_luau->ownsBattle( instance ) )
  {
    m_luau->onBattleUpdate( instance, tickCount );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );

  if( script )
  {
    script->onUpdate( instance, tickCount );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onDutyCommence( QuestBattle& instance, Entity::Player& player )
{
  if( m_luau->ownsBattle( instance ) )
  {
    m_luau->onDutyCommence( instance, player );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );

  if( script )
  {
    script->onDutyCommence( instance, player );
    return true;
  }

  return false;
}

bool Sapphire::Scripting::ScriptMgr::onInstanceEnterTerritory( QuestBattle& instance, Entity::Player& player,
                                                               uint32_t eventId, uint16_t param1, uint16_t param2 )
{
  if( m_luau->ownsBattle( instance ) )
  {
    m_luau->onBattleEnterTerritory( instance, player, eventId, param1, param2 );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );
  if( script )
  {
    script->onEnterTerritory( instance, player, eventId, param1, param2 );
    return true;
  }

  return false;
}

Sapphire::Scripting::NativeScriptMgr& Sapphire::Scripting::ScriptMgr::getNativeScriptHandler()
{
  return *m_nativeScriptMgr;
}

bool
Sapphire::Scripting::ScriptMgr::onDutyComplete( QuestBattle& instance, Sapphire::Entity::Player& player )
{
  if( m_luau->ownsBattle( instance ) )
  {
    m_luau->onDutyComplete( instance, player );
    return true;
  }

  auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestBattleScript >( instance.getDirectorId() );
  if( script )
  {
    World::Quest preQ;
    auto questIdx = player.getQuestIndex( instance.getQuestId() );
    auto& quest = player.getQuestByIndex( questIdx );
    preQ = quest;
    script->onDutyComplete( instance, player );
    if( quest != preQ )
      player.updateQuest( quest );

    return true;
  }

  return false;
}

void Sapphire::Scripting::ScriptMgr::onBattleChange( QuestBattle& instance )
{
  if( m_luau->ownsBattle( instance ) )
    m_luau->onBattleChange( instance );
}

bool Sapphire::Scripting::ScriptMgr::onSay( Sapphire::Entity::Player& player, uint64_t actorId, uint32_t eventId,
                                            uint32_t sayId )
{
  auto eventType = static_cast< uint16_t >( eventId >> 16 );
  auto& exdData = Common::Service< Data::ExdData >::ref();
  if( eventType == Event::EventHandler::EventHandlerType::Quest )
  {
    if( auto* luau = m_luau->questScript( eventId ) )
    {
      auto actorBaseId = Common::Service< World::Manager::EventMgr >::ref().getActorBaseId( static_cast< uint32_t >( actorId ) );
      m_luau->questHook( *luau, ScriptHandler::onSay, player, eventId, false, { double( actorBaseId ), double( sayId ) } );
      return true;
    }

    auto script = m_nativeScriptMgr->getScript< Sapphire::ScriptAPI::QuestScript >( eventId );
    if( !script )
    {
      auto questInfo = exdData.getRow< Excel::Quest >( eventId );
      if( questInfo )
      {
        World::Manager::PlayerMgr::sendUrgent( player, "Quest not implemented: {0} ({1})",
                                               questInfo->getString( questInfo->data().Text.Name ), eventId );
        return false;
      }
    }

    auto& pEventMgr = Common::Service< World::Manager::EventMgr >::ref();
    auto actor = pEventMgr.getActorBaseId( static_cast< uint32_t >( actorId ) );

    auto questId = static_cast< uint16_t >( eventId );
    if( player.hasQuest( eventId ) )
    {
      World::Quest preQ;
      auto questIdx = player.getQuestIndex( questId );
      auto& quest = player.getQuestByIndex( questIdx );
      preQ = quest;
      script->onSay( quest, player, actor, sayId );
      if( quest != preQ )
        player.updateQuest( quest );
    }
    else
    {
      auto newQuest = World::Quest( questId, 0, 0 );
      script->onSay( newQuest, player, actor, sayId );
    }
    return true;
  }
  return false;
}
