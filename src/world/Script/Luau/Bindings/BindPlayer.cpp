#include "Handles.h"
#include "../LuauScriptMgr.h"

#include <algorithm>

#include <Exd/ExdData.h>
#include <Service.h>

#include "Actor/Player.h"
#include "Event/EventHandler.h"
#include "Manager/EventMgr.h"
#include "Manager/PlayerMgr.h"
#include "Manager/WarpMgr.h"

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    using Player = Entity::Player;

    World::Manager::EventMgr& eventMgr()
    {
      return Common::Service< World::Manager::EventMgr >::ref();
    }

    // EventAction callbacks run inside Player::processActions; resuming there would let a continuation that starts
    // another eventAction replace the running action, so the resume is deferred to the next LuauScriptMgr::update
    void resumeAction( uint32_t threadId, uint64_t token, bool finished )
    {
      auto* mgr = LuauScriptMgr::instance();
      if( !mgr || !mgr->running() )
        return;
      mgr->deferActionResume( threadId, token, finished );
    }

    // shared by the finish/interrupt callbacks of one eventAction. An action replaced by Chara::setCurrentAction (or
    // never started) is destroyed without firing either callback; the last callback copy going away then resumes
    // the thread as interrupted instead of leaving it parked
    struct ActionWait
    {
      uint32_t threadId;
      uint64_t token;
      bool fired = false;

      ActionWait( uint32_t threadId, uint64_t token ) : threadId( threadId ), token( token ) {}
      ActionWait( const ActionWait& ) = delete;
      ActionWait& operator=( const ActionWait& ) = delete;
      ~ActionWait()
      {
        if( !fired )
          resumeAction( threadId, token, false );
      }

      void fire( bool finished )
      {
        if( fired )
          return;
        fired = true;
        resumeAction( threadId, token, finished );
      }
    };

    // player:eventAction( eventId, action ) -> boolean; yields until the cast finishes (true) or is interrupted (false)
    int l_eventAction( lua_State* L )
    {
      auto player = checkPlayer( L, 1 );
      auto eventId = Bind::Stack< uint32_t >::check( L, 2 );
      auto action = Bind::Stack< uint32_t >::check( L, 3 );
      auto& t = beginYield( L );

      auto& exdData = Common::Service< Data::ExdData >::ref();
      if( !exdData.getRow< Excel::EventAction >( action ) )
        luaL_error( L, "unknown event action %d", static_cast< int >( action ) );
      if( !player->getEvent( eventId ) && player->getEventCount() == 0 )
        luaL_error( L, "eventAction needs an open event (event %08X is not open and the player is in no event)", eventId );

      t.wait = WaitKind::Action;
      auto wait = std::make_shared< ActionWait >( t.id, luauMgr( L ).beginActionWait( t.id ) );
      eventMgr().eventActionStart( *player, eventId, action,
                                   [ wait ]( Player&, uint32_t, uint64_t ) { wait->fire( true ); },
                                   [ wait ]( Player&, uint32_t, uint64_t ) { wait->fire( false ); },
                                   eventId );
      wait.reset();
      return lua_yield( L, 0 );
    }

    int l_sceneAwait( lua_State* L )
    {
      auto token = Bind::Stack< uint64_t >::check( L, 1 );
      return luauMgr( L ).sceneAwait( L, token );
    }

    void eventStart( lua_State* L, Entity::PlayerPtr player, uint64_t actorId, uint32_t eventId, uint8_t param1,
                     uint32_t param2 )
    {
      auto& t = beginYield( L );
      if( player->getEvent( eventId ) )
        luaL_error( L, "event %08X is already open", eventId );
      // the no-op finish callback keeps eventFinish's Nest branch from sweeping the parent event
      eventMgr().eventStart( *player, actorId, eventId, Event::EventHandler::Nest, param1, param2,
                             []( Player&, uint64_t ) {} );
      t.events.push_back( OwnedEvent{ player, eventId, player->getEvent( eventId ) } );
    }

    void eventFinish( lua_State* L, Entity::PlayerPtr player, uint32_t eventId )
    {
      Event::EventHandlerPtr handler;
      if( auto* t = callingThread( L ) )
      {
        for( auto it = t->events.begin(); it != t->events.end(); ++it )
        {
          if( it->eventId == eventId && it->player.lock() == player )
          {
            handler = it->handler;
            t->events.erase( it );
            break;
          }
        }
      }
      auto current = player->getEvent( eventId );
      if( current && ( !handler || current == handler ) )
      {
        current->setPlayedScene( false );
        eventMgr().eventFinish( *player, eventId, 0 );
      }
    }
  }

  void bindPlayer( Bind::Registry& reg )
  {
    Bind::Class< ActorRef< Entity::Player > >( reg, "Player", HandleTag::Player )
      .inherits( "Chara" )
      .equality( actorEq )
      .prop( "territoryId", []( Player& p ) { return p.getTerritoryId(); } )
      .prop( "territoryTypeId", []( Player& p ) { return p.getTerritoryTypeId(); } )
      .prop( "partyId", []( Player& p ) { return p.getPartyId(); } )
      .prop( "inCombat", []( Player& p ) { return p.isInCombat(); } )
      .prop( "homepoint", []( Player& p ) { return p.getHomepoint(); } )
      .method( "hasQuest", []( Player& p, uint32_t id ) { return p.hasQuest( id ); }, "id" )
      .method( "isQuestCompleted", []( Player& p, uint32_t id ) { return p.isQuestCompleted( id ); }, "id" )
      .rawMethod( "quest", []( lua_State* L ) -> int
      {
        auto player = checkPlayer( L, 1 );
        auto id = Bind::Stack< uint32_t >::check( L, 2 );
        pushQuest( L, player, static_cast< uint16_t >( id & 0xFFFF ) );
        return 1;
      }, "(self, id: number) -> Quest" )
      .method( "finishQuest", []( Player& p, lua_State* L, uint32_t id, std::optional< uint32_t > choice )
      {
        auto questId = static_cast< uint16_t >( id );
        // drop this thread's cached copy, or flushQuests would write the finished quest back
        if( auto* t = callingThread( L ) )
        {
          auto& quests = t->quests;
          quests.erase( std::remove_if( quests.begin(), quests.end(), [ & ]( const QuestAccess& a )
          {
            auto owner = a.player.lock();
            return a.questId == questId && ( !owner || owner.get() == &p );
          } ), quests.end() );
        }
        p.finishQuest( questId, choice.value_or( 0 ) );
      }, "id, optionalChoice" )
      .method( "eventNotice", []( Player& p, uint32_t eventId, int32_t noticeId, std::optional< uint8_t > numArgs,
                                  std::optional< uint32_t > var1, std::optional< uint32_t > var2 )
      {
        eventMgr().sendEventNotice( p, eventId, static_cast< int8_t >( noticeId ), numArgs.value_or( 0 ), var1.value_or( 0 ),
                                    var2.value_or( 0 ) );
      }, "eventId, noticeId, numArgs, var1, var2" )
      .method( "sendDebug", []( Player& p, std::string msg ) { World::Manager::PlayerMgr::sendDebug( p, msg ); }, "msg" )
      .method( "sendUrgent", []( Player& p, std::string msg ) { World::Manager::PlayerMgr::sendUrgent( p, msg ); }, "msg" )
      .rawMethod( "eventAction", l_eventAction, "(self, eventId: number, action: number) -> boolean" )
      .method( "setPos", []( Player& p, Common::Vector3 pos, std::optional< float > rot )
      {
        p.setPos( pos );
        if( rot )
          p.setRot( *rot );
      }, "pos, rot" )
      .method( "setOnEnterEventDone", []( Player& p, bool done ) { p.setOnEnterEventDone( done ); }, "done" )
      .method( "teleport", []( Player& p, uint16_t aetheryteId, uint8_t teleportType )
      {
        Common::Service< World::Manager::WarpMgr >::ref().requestPlayerTeleport( p, aetheryteId, teleportType );
      }, "aetheryteId, teleportType" )
      .method( "skillProc", []( Player& p, uint8_t index )
      {
        Common::Service< World::Manager::PlayerMgr >::ref().onSkillProc( p, index );
      }, "index" )
      .method( "exitInstance", []( Player& p ) { Common::Service< World::Manager::PlayerMgr >::ref().onExitInstance( p ); } )
      .method( "currency", []( Player& p, Common::CurrencyType type ) { return p.getCurrency( type ); }, "type" )
      .method( "removeCurrency", []( Player& p, Common::CurrencyType type, uint32_t amount ) { p.removeCurrency( type, amount ); },
               "type, amount" )
      .method( "removeItem", []( Player& p, uint32_t catalogId, std::optional< uint32_t > quantity )
      {
        return p.removeItem( catalogId, quantity.value_or( 1 ) );
      }, "catalogId, quantity" )
      .luauMethod( "playScene", "(self, eventId: number, scene: number, flags: (number | {number})?, params: {number}?, "
                                "onYield: SceneYieldHandler?) -> SceneResult" )
      .luauMethod( "nest", "(self, actorId: number, eventId: number, body: (child: ChildEvent) -> ...any, "
                           "opts: NestOptions?) -> ...any" );

    Bind::Library( reg, "__host", true )
      .function( "scene_begin", []( lua_State* L, Entity::PlayerPtr player, uint32_t eventId, uint32_t scene,
                                    Bind::Flags flags, std::vector< uint32_t > params ) -> double
      {
        return static_cast< double >( luauMgr( L ).sceneBegin( L, *player, eventId, scene, flags.value, params ) );
      } )
      .rawFunction( "scene_await", l_sceneAwait, "(token: number) -> any" )
      .function( "scene_resume", []( lua_State* L, double token, uint8_t yieldId, std::vector< uint32_t > values )
      {
        luauMgr( L ).sceneResume( L, static_cast< uint64_t >( token ), yieldId, values );
      } )
      .function( "event_start", []( lua_State* L, Entity::PlayerPtr player, uint64_t actorId, uint32_t eventId,
                                    uint8_t param1, uint32_t param2 )
      {
        eventStart( L, std::move( player ), actorId, eventId, param1, param2 );
      } )
      .function( "event_finish", []( lua_State* L, Entity::PlayerPtr player, uint32_t eventId )
      {
        eventFinish( L, std::move( player ), eventId );
      } );
  }
}
