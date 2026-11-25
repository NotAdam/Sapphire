// Action and ActionResults handles. Both are hook-scoped: they hold a raw pointer plus the serial of the hook that
// created them and raise "<Type> is only valid inside its handler" once that hook has returned.

#include "Handles.h"
#include "../LuauScriptMgr.h"

#include "Action/Action.h"
#include "Action/ActionLut.h"
#include "Action/ActionResultBuilder.h"
#include "Actor/Chara.h"
#include "Math/CalcStats.h"

namespace Sapphire::Scripting::Bindings
{
  struct ActionStorage
  {
    World::Action::Action* action;
    uint64_t serial;
  };

  struct ResultsStorage
  {
    World::Action::ActionResultBuilderPtr builder;
    uint64_t serial;
  };

  struct ActionTag
  {
  };

  struct ResultsTag
  {
  };

  struct ResultsHandle
  {
    World::Action::ActionResultBuilderPtr builder;
  };
}

namespace Sapphire::Scripting::Bind
{
  template<>
  struct Target< Bindings::ActionTag >
  {
    using Storage = Bindings::ActionStorage;
    static World::Action::Action* lock( lua_State* L, Storage& s, const char* typeName )
    {
      if( !Bindings::luauMgr( L ).isHookLive( s.serial ) )
        luaL_error( L, "%s is only valid inside its handler", typeName );
      return s.action;
    }
  };

  template<>
  struct Target< Bindings::ResultsTag >
  {
    using Storage = Bindings::ResultsStorage;
    static World::Action::ActionResultBuilder* lock( lua_State* L, Storage& s, const char* typeName )
    {
      if( !s.builder || !Bindings::luauMgr( L ).isHookLive( s.serial ) )
        luaL_error( L, "%s is only valid inside its handler", typeName );
      return s.builder.get();
    }
  };

  template<>
  struct Stack< Bindings::ResultsHandle >
  {
    static int push( lua_State* L, const Bindings::ResultsHandle& h )
    {
      if( !h.builder )
      {
        lua_pushnil( L );
        return 1;
      }
      Class< Bindings::ResultsTag >::push( L, Bindings::HandleTag::ActionResults,
                                           Bindings::ResultsStorage{ h.builder, Bindings::luauMgr( L ).currentHookSerial() } );
      return 1;
    }
    static std::string luauType() { return "ActionResults?"; }
  };
}

namespace Sapphire::Scripting::Bindings
{
  void pushAction( lua_State* L, World::Action::Action* action )
  {
    if( !action )
    {
      lua_pushnil( L );
      return;
    }
    Bind::Class< ActionTag >::push( L, HandleTag::Action, ActionStorage{ action, luauMgr( L ).currentHookSerial() } );
  }

  void bindAction( Bind::Registry& reg )
  {
    using Action = World::Action::Action;
    using Builder = World::Action::ActionResultBuilder;

    Bind::Class< ActionTag >( reg, "Action", HandleTag::Action )
      .prop( "id", []( Action& a ) { return a.getId(); } )
      .prop( "source", []( Action& a ) { return a.getSourceChara(); } )
      .prop( "hitTarget", []( Action& a ) { return nullable( a.getHitChara() ); } )
      .prop( "hitTargets", []( Action& a ) { return a.getHitCharas(); } )
      .prop( "results", []( Action& a ) { return ResultsHandle{ a.getActionResultBuilder() }; } )
      .prop( "castTime", []( Action& a ) { return a.getCastTime(); }, []( Action& a, uint32_t ms ) { a.setCastTime( ms ); } )
      .method( "calcDamage", []( Action& a, uint32_t potency ) { return a.calcDamage( potency ); }, "potency" )
      .method( "calcHealing", []( Action& a, uint32_t potency ) { return a.calcHealing( potency ); }, "potency" )
      .method( "interrupt", []( Action& a ) { a.interrupt(); } );

    Bind::Class< ResultsTag >( reg, "ActionResults", HandleTag::ActionResults )
      .method( "damage", []( Builder& b, Entity::CharaPtr effectTarget, Entity::CharaPtr target, uint32_t amount,
                             std::optional< Common::CalcResultType > kind, std::optional< Common::ActionResultFlag > flag )
      {
        b.damage( effectTarget, target, amount, kind.value_or( Common::CalcResultType::TypeDamageHp ),
                  flag.value_or( Common::ActionResultFlag::None ) );
      }, "effectTarget, target, amount, kind, flag" )
      .method( "heal", []( Builder& b, Entity::CharaPtr effectTarget, Entity::CharaPtr target, uint32_t amount,
                           std::optional< Common::CalcResultType > kind, std::optional< Common::ActionResultFlag > flag )
      {
        b.heal( effectTarget, target, amount, kind.value_or( Common::CalcResultType::TypeRecoverMp ),
                flag.value_or( Common::ActionResultFlag::None ) );
      }, "effectTarget, target, amount, kind, flag" )
      .method( "restoreMp", []( Builder& b, Entity::CharaPtr effectTarget, Entity::CharaPtr target, uint32_t amount,
                                std::optional< Common::ActionResultFlag > flag )
      {
        b.restoreMP( effectTarget, target, amount, flag.value_or( Common::ActionResultFlag::None ) );
      }, "effectTarget, target, amount, flag" )
      .method( "startCombo", []( Builder& b, Entity::CharaPtr target, uint16_t actionId ) { b.startCombo( target, actionId ); },
               "target, actionId" )
      .method( "comboSucceed", []( Builder& b, Entity::CharaPtr target ) { b.comboSucceed( target ); }, "target" )
      .method( "knockback", []( Builder& b, Entity::CharaPtr target, uint32_t kbExdKey, float offset )
      {
        b.knockback( target, kbExdKey, offset );
      }, "target, kbExdKey, offset" )
      .method( "applyStatus", []( Builder& b, Entity::CharaPtr target, uint16_t statusId, uint32_t duration, uint8_t param,
                                  std::optional< Modifiers > modifiers, std::optional< uint32_t > flag,
                                  std::optional< bool > toSource, std::optional< bool > override )
      {
        if( !modifiers )
          b.applyStatusEffect( target, statusId, duration, param, override.value_or( false ) );
        else
          b.applyStatusEffect( target, statusId, duration, param, modifiers->list, flag.value_or( 0 ),
                               toSource.value_or( false ), override.value_or( false ) );
      }, "target, statusId, duration, param, modifiers, flag, toSource, override" )
      .method( "applyStatusSelf", []( Builder& b, uint16_t statusId, uint32_t duration, uint8_t param,
                                      std::optional< Modifiers > modifiers, std::optional< uint32_t > flag,
                                      std::optional< bool > override )
      {
        if( !modifiers )
          b.applyStatusEffectSelf( statusId, duration, param, override.value_or( false ) );
        else
          b.applyStatusEffectSelf( statusId, duration, param, modifiers->list, flag.value_or( 0 ), override.value_or( false ) );
      }, "statusId, duration, param, modifiers, flag, override" );

    Bind::Library( reg, "calc" )
      .function( "mpRefresh", []( uint32_t potency, uint8_t level ) { return Math::CalcStats::calcMpRefresh( potency, level ); },
                 "potency, level" );
  }
}
