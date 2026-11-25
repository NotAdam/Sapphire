#include "Handles.h"
#include "../LuauScriptMgr.h"

#include <Actor/BNpc.h>
#include <Actor/Chara.h>
#include <Actor/EventObject.h>
#include <Actor/GameObject.h>
#include <Actor/Player.h>
#include <StatusEffect/StatusEffect.h>

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    constexpr uint64_t bit( int tag )
    {
      return uint64_t( 1 ) << tag;
    }

    void pushActorTagged( lua_State* L, int tag, const Entity::GameObjectPtr& obj )
    {
      void* mem = lua_newuserdatataggedwithmetatable( L, sizeof( ActorStorage ), tag );
      new( mem ) ActorStorage{ obj };
    }

    Entity::GameObjectPtr checkActorTag( lua_State* L, int idx, uint64_t mask, const char* expected )
    {
      int tag = lua_userdatatag( L, idx );
      if( tag < 0 || tag >= 64 || !( mask & bit( tag ) ) )
        luaL_typeerror( L, idx, expected );
      auto* s = static_cast< ActorStorage* >( lua_touserdata( L, idx ) );
      auto p = s->obj.lock();
      if( !p )
        luaL_error( L, "%s is no longer valid", expected );
      return p;
    }

    std::vector< Entity::CharaPtr > inRange( Entity::Chara& c, std::optional< bool > includeSelf )
    {
      std::vector< Entity::CharaPtr > out;
      for( auto& obj : c.getInRangeActors( includeSelf.value_or( false ) ) )
        if( auto chara = obj ? obj->getAsChara() : nullptr )
          out.push_back( chara );
      return out;
    }
  }

  LuauScriptMgr& luauMgr( lua_State* L )
  {
    auto* mgr = LuauScriptMgr::instance();
    if( !mgr )
      luaL_error( L, "Luau scripting is not running" );
    return *mgr;
  }

  template< typename T >
  std::shared_ptr< T > actorCast( const Entity::GameObjectPtr& p )
  {
    return std::static_pointer_cast< T >( p );
  }

  template std::shared_ptr< Entity::GameObject > actorCast< Entity::GameObject >( const Entity::GameObjectPtr& );
  template std::shared_ptr< Entity::Chara > actorCast< Entity::Chara >( const Entity::GameObjectPtr& );
  template std::shared_ptr< Entity::Player > actorCast< Entity::Player >( const Entity::GameObjectPtr& );
  template std::shared_ptr< Entity::BNpc > actorCast< Entity::BNpc >( const Entity::GameObjectPtr& );
  template std::shared_ptr< Entity::EventObject > actorCast< Entity::EventObject >( const Entity::GameObjectPtr& );

  Entity::GameObjectPtr lockActor( lua_State* L, ActorStorage& s, const char* typeName )
  {
    auto p = s.obj.lock();
    if( !p )
      luaL_error( L, "%s is no longer valid", typeName );
    return p;
  }

  int actorEq( lua_State* L )
  {
    auto* a = static_cast< ActorStorage* >( lua_touserdata( L, 1 ) );
    auto* b = static_cast< ActorStorage* >( lua_touserdata( L, 2 ) );
    bool eq = a && b && !a->obj.owner_before( b->obj ) && !b->obj.owner_before( a->obj );
    lua_pushboolean( L, eq ? 1 : 0 );
    return 1;
  }

  void pushGameObject( lua_State* L, const Entity::GameObjectPtr& obj )
  {
    if( !obj )
      lua_pushnil( L );
    else if( obj->isPlayer() )
      pushActorTagged( L, HandleTag::Player, obj );
    else if( obj->isBattleNpc() )
      pushActorTagged( L, HandleTag::BNpc, obj );
    else if( obj->isEventObj() )
      pushActorTagged( L, HandleTag::EObj, obj );
    else if( obj->isChara() )
      pushActorTagged( L, HandleTag::Chara, obj );
    else
      lua_pushnil( L );
  }

  void pushPlayer( lua_State* L, const Entity::PlayerPtr& obj )
  {
    pushGameObject( L, obj );
  }

  void pushBNpc( lua_State* L, const Entity::BNpcPtr& obj )
  {
    pushGameObject( L, obj );
  }

  void pushEObj( lua_State* L, const Entity::EventObjectPtr& obj )
  {
    pushGameObject( L, obj );
  }

  void pushChara( lua_State* L, const Entity::CharaPtr& obj )
  {
    pushGameObject( L, obj );
  }

  Entity::PlayerPtr checkPlayer( lua_State* L, int idx )
  {
    return std::static_pointer_cast< Entity::Player >( checkActorTag( L, idx, bit( HandleTag::Player ), "Player" ) );
  }

  Entity::BNpcPtr checkBNpc( lua_State* L, int idx )
  {
    return std::static_pointer_cast< Entity::BNpc >( checkActorTag( L, idx, bit( HandleTag::BNpc ), "BNpc" ) );
  }

  Entity::EventObjectPtr checkEObj( lua_State* L, int idx )
  {
    return std::static_pointer_cast< Entity::EventObject >( checkActorTag( L, idx, bit( HandleTag::EObj ), "EObj" ) );
  }

  Entity::CharaPtr checkChara( lua_State* L, int idx )
  {
    auto mask = bit( HandleTag::Player ) | bit( HandleTag::BNpc ) | bit( HandleTag::Chara );
    return std::static_pointer_cast< Entity::Chara >( checkActorTag( L, idx, mask, "Chara" ) );
  }

  void bindActors( Bind::Registry& reg )
  {
    using Chara = Entity::Chara;

    Bind::Class< ActorRef< Entity::Chara > >( reg, "Chara", HandleTag::Chara )
      .equality( actorEq )
      .prop( "id", []( Chara& c ) { return c.getId(); } )
      .prop( "name", []( Chara& c ) { return c.getName(); } )
      .prop( "level", []( Chara& c ) { return c.getLevel(); } )
      .prop( "classJob", []( Chara& c ) { return c.getClass(); } )
      .prop( "hp", []( Chara& c ) { return c.getHp(); } )
      .prop( "maxHp", []( Chara& c ) { return c.getMaxHp(); } )
      .prop( "hpPercent", []( Chara& c ) { return c.getHpPercent(); } )
      .prop( "pos", []( Chara& c ) { return c.getPos(); } )
      .prop( "alive", []( Chara& c ) { return c.isAlive(); } )
      .prop( "isPlayer", []( Chara& c ) { return c.isPlayer(); } )
      .prop( "isBNpc", []( Chara& c ) { return c.isBattleNpc(); } )
      .prop( "mp", []( Chara& c ) { return c.getMp(); }, []( Chara& c, uint32_t v ) { c.setMp( v ); } )
      .prop( "tp", []( Chara& c ) { return c.getTp(); }, []( Chara& c, uint32_t v ) { c.setTp( v ); } )
      .method( "hasStatusEffect", []( Chara& c, uint32_t id ) { return c.hasStatusEffect( id ); }, "id" )
      .rawMethod( "statusEffect", []( lua_State* L ) -> int
      {
        auto c = checkChara( L, 1 );
        auto id = Bind::Stack< uint32_t >::check( L, 2 );
        pushStatusEffectShared( L, c->getStatusEffectById( id ) );
        return 1;
      }, "(self, id: number) -> StatusEffect?" )
      .method( "removeStatusEffect", []( Chara& c, uint32_t id ) { c.removeSingleStatusEffectById( id ); }, "id" )
      .method( "removeStatusEffectByFlag", []( Chara& c, Common::StatusEffectFlag flag ) { c.removeStatusEffectByFlag( flag ); },
               "flag" )
      .method( "modifier", []( Chara& c, Common::ParamModifier m ) { return c.getModifier( m ); }, "paramModifier" )
      .method( "heal", []( Chara& c, uint32_t amount ) { c.heal( amount ); }, "amount" )
      .method( "setInvincibility", []( Chara& c, Common::InvincibilityType t ) { c.setInvincibilityType( t ); }, "type" )
      .method( "knockback", []( Chara& c, Common::Vector3 origin, float distance ) { c.knockback( origin, distance ); },
               "origin, distance" )
      .method( "isHostile", []( Chara& c, Entity::CharaPtr other ) { return c.isHostile( *other ); }, "other" )
      .method( "isFriendly", []( Chara& c, Entity::CharaPtr other ) { return c.isFriendly( *other ); }, "other" )
      .method( "weaponDamage", []( Chara& c, lua_State* L, std::string kind ) -> float
      {
        if( kind == "physical" )
          return c.getPhysicalWeaponDamage();
        if( kind == "magical" )
          return c.getMagicalWeaponDamage();
        luaL_error( L, "weaponDamage: expected \"physical\" or \"magical\", got \"%s\"", kind.c_str() );
        return 0;
      }, "kind" )
      .method( "inRange", []( Chara& c, std::optional< bool > includeSelf ) { return inRange( c, includeSelf ); },
               "includeSelf" );

    Bind::Class< ActorRef< Entity::BNpc > >( reg, "BNpc", HandleTag::BNpc )
      .inherits( "Chara" )
      .equality( actorEq )
      .prop( "layoutId", []( Entity::BNpc& b ) { return b.getLayoutId(); } )
      .prop( "nameId", []( Entity::BNpc& b ) { return b.getBNpcNameId(); } )
      .method( "setFlag", []( Entity::BNpc& b, Bind::Flags flags ) { b.setFlag( flags.value ); }, "flags" )
      .method( "hateAdd", []( Entity::BNpc& b, Entity::CharaPtr target, int32_t amount ) { b.hateListAdd( target, amount ); },
               "target, amount" );

    using EObj = Entity::EventObject;
    Bind::Class< ActorRef< Entity::EventObject > >( reg, "EObj", HandleTag::EObj )
      .equality( actorEq )
      .prop( "id", []( EObj& e ) { return e.getId(); } )
      .prop( "name", []( EObj& e ) { return e.getName(); } )
      .prop( "baseId", []( EObj& e ) { return e.getBaseId(); } )
      .prop( "instanceId", []( EObj& e ) { return e.getInstanceId(); } )
      .prop( "state", []( EObj& e ) { return e.getState(); }, []( EObj& e, uint8_t s ) { e.setState( s ); } )
      .method( "show", []( EObj& e ) { e.setPermissionInvisibility( 0 ); } )
      .method( "hide", []( EObj& e ) { e.setPermissionInvisibility( 1 ); } )
      .method( "setInvisibility", []( EObj& e, uint8_t n ) { e.setPermissionInvisibility( n ); }, "value" )
      .method( "setCollision", []( EObj& e, bool enabled ) { e.setCollisionEnabled( enabled ); }, "enabled" )
      .method( "addCollisionBox", []( EObj& e, Common::Vector3 pos, float rotation, float width, float height, float depth )
      {
        e.addCollisionBox( pos, rotation, width, height, depth );
      }, "pos, rotation, width, height, depth" );
  }
}
