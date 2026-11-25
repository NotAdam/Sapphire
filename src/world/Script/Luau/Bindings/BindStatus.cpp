// StatusEffect handles and the status library. A StatusEffect passed to a hook is hook-scoped (raw pointer + serial);
// one obtained from chara:statusEffect( id ) holds a weak_ptr instead and stays valid while the effect exists.

#include "Handles.h"
#include "../LuauScriptMgr.h"

#include <Service.h>

#include "Action/ActionLut.h"
#include "Actor/Chara.h"
#include "Manager/StatusEffectMgr.h"
#include "StatusEffect/StatusEffect.h"

namespace Sapphire::Scripting::Bindings
{
  struct StatusStorage
  {
    std::weak_ptr< StatusEffect::StatusEffect > weak;
    StatusEffect::StatusEffect* raw;
    uint64_t serial;
  };

  struct StatusTag
  {
  };
}

namespace Sapphire::Scripting::Bind
{
  template<>
  struct Target< Bindings::StatusTag >
  {
    using Storage = Bindings::StatusStorage;
    static std::shared_ptr< StatusEffect::StatusEffect > lock( lua_State* L, Storage& s, const char* typeName )
    {
      if( s.raw )
      {
        if( !Bindings::luauMgr( L ).isHookLive( s.serial ) )
          luaL_error( L, "%s is only valid inside its handler", typeName );
        // non-owning: the hook's caller keeps the effect alive for the duration of the handler
        return std::shared_ptr< StatusEffect::StatusEffect >( std::shared_ptr< StatusEffect::StatusEffect >(), s.raw );
      }
      auto p = s.weak.lock();
      if( !p )
        luaL_error( L, "%s is no longer valid", typeName );
      return p;
    }
  };

  Bindings::Modifiers Stack< Bindings::Modifiers >::check( lua_State* L, int idx )
  {
    idx = lua_absindex( L, idx );
    luaL_checktype( L, idx, LUA_TTABLE );
    Bindings::Modifiers out;
    int n = lua_objlen( L, idx );
    out.list.reserve( static_cast< size_t >( n ) );
    for( int i = 1; i <= n; ++i )
    {
      lua_rawgeti( L, idx, i );
      if( !lua_istable( L, -1 ) )
        luaL_error( L, "modifier %d must be { ParamModifier, value }", i );
      int entry = lua_gettop( L );
      lua_rawgeti( L, entry, 1 );
      lua_rawgeti( L, entry, 2 );
      auto modifier = Stack< Common::ParamModifier >::check( L, entry + 1 );
      auto value = Stack< int32_t >::check( L, entry + 2 );
      out.list.push_back( World::Action::StatusModifier{ modifier, value } );
      lua_pop( L, 3 );
    }
    return out;
  }
}

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    World::Manager::StatusEffectMgr& statusMgr()
    {
      return Common::Service< World::Manager::StatusEffectMgr >::ref();
    }

    int l_modifiers( lua_State* L )
    {
      auto* s = static_cast< StatusStorage* >( lua_touserdata( L, 1 ) );
      if( lua_userdatatag( L, 1 ) != HandleTag::StatusEffect || !s )
        luaL_typeerror( L, 1, "StatusEffect" );
      auto effect = Bind::Target< StatusTag >::lock( L, *s, "StatusEffect" );
      lua_newtable( L );
      for( auto& [ modifier, value ] : effect->getModifiers() )
      {
        lua_pushnumber( L, static_cast< double >( value ) );
        lua_rawseti( L, -2, static_cast< int >( modifier ) );
      }
      return 1;
    }
  }

  void pushStatusEffect( lua_State* L, StatusEffect::StatusEffect* effect )
  {
    if( !effect )
    {
      lua_pushnil( L );
      return;
    }
    Bind::Class< StatusTag >::push( L, HandleTag::StatusEffect,
                                    StatusStorage{ {}, effect, luauMgr( L ).currentHookSerial() } );
  }

  void pushStatusEffectShared( lua_State* L, const StatusEffect::StatusEffectPtr& effect )
  {
    if( !effect )
    {
      lua_pushnil( L );
      return;
    }
    Bind::Class< StatusTag >::push( L, HandleTag::StatusEffect, StatusStorage{ effect, nullptr, 0 } );
  }

  void bindStatus( Bind::Registry& reg )
  {
    using Effect = StatusEffect::StatusEffect;

    Bind::Class< StatusTag >( reg, "StatusEffect", HandleTag::StatusEffect )
      .prop( "id", []( Effect& e ) { return e.getId(); } )
      .prop( "duration", []( Effect& e ) { return e.getDuration(); } )
      .prop( "source", []( Effect& e ) { return nullable( e.getSrcActor() ); } )
      .prop( "param", []( Effect& e ) { return e.getParam(); }, []( Effect& e, uint16_t p ) { e.setParam( p ); } )
      .method( "setModifier", []( Effect& e, Common::ParamModifier modifier, int32_t value ) { e.setModifier( modifier, value ); },
               "paramModifier, value" )
      .rawMethod( "modifiers", l_modifiers, "(self) -> { [number]: number }" )
      .method( "refresh", []( Effect& e, std::optional< uint32_t > durationMs )
      {
        e.refresh( durationMs.value_or( e.getDuration() ) );
      }, "durationMs" );

    Bind::Library( reg, "status" )
      .function( "apply", []( Entity::CharaPtr source, Entity::CharaPtr target, uint32_t id, int32_t duration, uint8_t param,
                              Modifiers modifiers, std::optional< uint32_t > flag, std::optional< bool > override )
      {
        statusMgr().applyStatusEffect( source, target, id, duration, param, modifiers.list, flag.value_or( 0 ),
                                       override.value_or( false ) );
      }, "source, target, id, duration, param, modifiers, flag, override" )
      .function( "damage", []( Entity::CharaPtr source, Entity::CharaPtr target, uint32_t amount )
      {
        statusMgr().damage( source, target, amount );
      }, "source, target, amount" )
      .function( "heal", []( Entity::CharaPtr source, Entity::CharaPtr target, uint32_t amount )
      {
        statusMgr().heal( source, target, amount );
      }, "source, target, amount" );
  }
}
