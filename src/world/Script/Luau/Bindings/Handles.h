#pragma once

// Handle types shared by the world bindings: actors (Player, BNpc, EObj, Chara), quests, vectors, modifier lists.

#include "Bindings.h"

#include <Common.h>
#include <ForwardsZone.h>

#include "Action/ActionLut.h"

#include <optional>
#include <vector>

namespace Sapphire::Scripting::Bindings
{
  // every actor handle stores the same payload; the class decides the target type
  struct ActorStorage
  {
    std::weak_ptr< Entity::GameObject > obj;
  };

  template< typename T >
  struct ActorRef
  {
  };

  struct QuestStorage
  {
    std::weak_ptr< Entity::Player > player;
    uint16_t questId;
  };

  struct QuestTag
  {
  };

  // the calling thread's view of a quest (see QuestAccess)
  struct QuestView
  {
    World::Quest* quest;
    Entity::PlayerPtr player;
    uint16_t questId;
  };

  // { { ParamModifier.X, value }, ... }
  struct Modifiers
  {
    std::vector< World::Action::StatusModifier > list;
  };

  LuauScriptMgr& luauMgr( lua_State* L );

  // push the most specific handle (Player, BNpc, EObj, Chara) or nil
  void pushGameObject( lua_State* L, const Entity::GameObjectPtr& obj );
  void pushPlayer( lua_State* L, const Entity::PlayerPtr& obj );
  void pushBNpc( lua_State* L, const Entity::BNpcPtr& obj );
  void pushEObj( lua_State* L, const Entity::EventObjectPtr& obj );
  void pushChara( lua_State* L, const Entity::CharaPtr& obj );
  void pushQuest( lua_State* L, const Entity::PlayerPtr& player, uint16_t questId );
  void pushAction( lua_State* L, World::Action::Action* action );
  void pushStatusEffect( lua_State* L, StatusEffect::StatusEffect* effect );
  // a StatusEffect handle that stays valid as long as the effect exists (chara:statusEffect)
  void pushStatusEffectShared( lua_State* L, const StatusEffect::StatusEffectPtr& effect );

  Entity::PlayerPtr checkPlayer( lua_State* L, int idx );
  Entity::BNpcPtr checkBNpc( lua_State* L, int idx );
  Entity::EventObjectPtr checkEObj( lua_State* L, int idx );
  Entity::CharaPtr checkChara( lua_State* L, int idx );

  // locks an actor payload, raising "<Type> is no longer valid"
  Entity::GameObjectPtr lockActor( lua_State* L, ActorStorage& s, const char* typeName );
  std::optional< QuestView > lockQuest( lua_State* L, QuestStorage& s );

  int actorEq( lua_State* L );

  // a pointer that may be null, typed `T?` in the definitions
  template< typename P >
  std::optional< P > nullable( P p )
  {
    if( !p )
      return std::nullopt;
    return p;
  }

  template< typename T >
  std::shared_ptr< T > actorCast( const Entity::GameObjectPtr& p );
}

namespace Sapphire::Scripting::Bind
{
  template< typename T >
  struct Target< Bindings::ActorRef< T > >
  {
    using Storage = Bindings::ActorStorage;
    static std::shared_ptr< T > lock( lua_State* L, Storage& s, const char* typeName )
    {
      return Bindings::actorCast< T >( Bindings::lockActor( L, s, typeName ) );
    }
  };

  template<>
  struct Target< Bindings::QuestTag >
  {
    using Storage = Bindings::QuestStorage;
    static std::optional< Bindings::QuestView > lock( lua_State* L, Storage& s, const char* )
    {
      return Bindings::lockQuest( L, s );
    }
  };

  template<>
  struct Stack< Common::Vector3 >
  {
    static int push( lua_State* L, const Common::Vector3& v )
    {
      lua_pushvector( L, v.x, v.y, v.z );
      return 1;
    }
    static Common::Vector3 check( lua_State* L, int idx )
    {
      const float* v = luaL_checkvector( L, idx );
      return { v[ 0 ], v[ 1 ], v[ 2 ] };
    }
    static std::string luauType() { return "vector"; }
  };

  template<>
  struct Stack< Bindings::Modifiers >
  {
    static Bindings::Modifiers check( lua_State* L, int idx );
    static std::string luauType() { return "{ {number} }"; }
  };

  template<>
  struct Stack< Entity::PlayerPtr >
  {
    static int push( lua_State* L, const Entity::PlayerPtr& v ) { Bindings::pushPlayer( L, v ); return 1; }
    static Entity::PlayerPtr check( lua_State* L, int idx ) { return Bindings::checkPlayer( L, idx ); }
    static std::string luauType() { return "Player"; }
  };

  template<>
  struct Stack< Entity::BNpcPtr >
  {
    static int push( lua_State* L, const Entity::BNpcPtr& v ) { Bindings::pushBNpc( L, v ); return 1; }
    static Entity::BNpcPtr check( lua_State* L, int idx ) { return Bindings::checkBNpc( L, idx ); }
    static std::string luauType() { return "BNpc"; }
  };

  template<>
  struct Stack< Entity::EventObjectPtr >
  {
    static int push( lua_State* L, const Entity::EventObjectPtr& v ) { Bindings::pushEObj( L, v ); return 1; }
    static Entity::EventObjectPtr check( lua_State* L, int idx ) { return Bindings::checkEObj( L, idx ); }
    static std::string luauType() { return "EObj"; }
  };

  template<>
  struct Stack< Entity::CharaPtr >
  {
    static int push( lua_State* L, const Entity::CharaPtr& v ) { Bindings::pushChara( L, v ); return 1; }
    static Entity::CharaPtr check( lua_State* L, int idx ) { return Bindings::checkChara( L, idx ); }
    static std::string luauType() { return "Chara"; }
  };
}
