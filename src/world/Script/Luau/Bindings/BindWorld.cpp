#include "Handles.h"

#include <Exd/ExdData.h>
#include <Service.h>

#include "Actor/Player.h"
#include "Manager/LinkshellMgr.h"

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    World::Manager::LinkshellMgr& linkshellMgr()
    {
      return Common::Service< World::Manager::LinkshellMgr >::ref();
    }

    // exd.switchTalk( eventId ) -> { { condition, talk } x 16 }?
    int l_switchTalk( lua_State* L )
    {
      auto eventId = Bind::Stack< uint32_t >::check( L, 1 );
      auto& exdData = Common::Service< Data::ExdData >::ref();
      auto row = exdData.getRow< Excel::SwitchTalk >( eventId );
      if( !row )
      {
        lua_pushnil( L );
        return 1;
      }
      const auto& cases = row->data().TalkCase;
      lua_createtable( L, 16, 0 );
      for( int i = 0; i < 16; ++i )
      {
        lua_createtable( L, 0, 2 );
        lua_pushnumber( L, cases[ i ].CaseCondition );
        lua_setfield( L, -2, "condition" );
        lua_pushnumber( L, cases[ i ].Talk );
        lua_setfield( L, -2, "talk" );
        lua_rawseti( L, -2, i + 1 );
      }
      return 1;
    }
  }

  void bindWorld( Bind::Registry& reg )
  {
    reg.typeAlias( "SwitchTalkCase", "{ condition: number, talk: number }" );

    Bind::Library( reg, "exd" )
      .rawFunction( "switchTalk", l_switchTalk, "(eventId: number) -> {SwitchTalkCase}?" );

    Bind::Library( reg, "linkshell" )
      .function( "create", []( std::string name, Entity::PlayerPtr player )
      {
        return linkshellMgr().createLinkshell( name, *player ) != nullptr;
      }, "name, player" )
      .function( "rename", []( uint64_t id, std::string name, Entity::PlayerPtr player )
      {
        return linkshellMgr().renameLinkshell( id, name, *player );
      }, "id, name, player" )
      .function( "finishAction", []( std::string name, uint32_t result, Entity::PlayerPtr player, uint8_t action )
      {
        linkshellMgr().finishLinkshellAction( name, result, *player, action );
      }, "name, result, player, action" );
  }
}
