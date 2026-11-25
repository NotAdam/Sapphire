#include "Handles.h"
#include "../LuauScriptMgr.h"

#include "Actor/Player.h"
#include "Quest/Quest.h"

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    int questEq( lua_State* L )
    {
      auto* a = static_cast< QuestStorage* >( lua_touserdata( L, 1 ) );
      auto* b = static_cast< QuestStorage* >( lua_touserdata( L, 2 ) );
      bool eq = a && b && a->questId == b->questId && !a->player.owner_before( b->player ) &&
                !b->player.owner_before( a->player );
      lua_pushboolean( L, eq ? 1 : 0 );
      return 1;
    }
  }

  void pushQuest( lua_State* L, const Entity::PlayerPtr& player, uint16_t questId )
  {
    void* mem = lua_newuserdatataggedwithmetatable( L, sizeof( QuestStorage ), HandleTag::Quest );
    new( mem ) QuestStorage{ player, questId };
  }

  // All reads and writes of a quest go through the calling thread's QuestAccess: the first touch in a segment takes a
  // copy of the player's quest (or binds the engine's quest object during a quest scene callback); the copy is written
  // back with Player::updateQuest when the segment ends (LuauScriptMgr::flushQuests).
  std::optional< QuestView > lockQuest( lua_State* L, QuestStorage& s )
  {
    auto* t = callingThread( L );
    if( !t )
      luaL_error( L, "Quest can only be used from a script thread" );
    auto player = s.player.lock();
    if( !player )
      luaL_error( L, "Quest is no longer valid" );

    for( auto& a : t->quests )
      if( a.questId == s.questId && a.player.lock() == player )
        return QuestView{ a.bound ? a.bound : &a.working, player, s.questId };

    QuestAccess access;
    access.player = player;
    access.questId = s.questId;
    if( t->boundQuest && t->boundQuest->getId() == s.questId && t->playerId == player->getId() )
      access.bound = t->boundQuest;
    else
    {
      if( player->hasQuest( s.questId ) )
        access.working = player->getQuestByIndex( static_cast< uint16_t >( player->getQuestIndex( s.questId ) ) );
      else
        access.working = World::Quest( s.questId, 0, 0 );
      access.snapshot = access.working;
    }
    t->quests.push_back( access );
    auto& a = t->quests.back();
    return QuestView{ a.bound ? a.bound : &a.working, player, s.questId };
  }

#define QUEST_PROP( luauName, getter, setter, type ) \
  .prop( luauName, []( QuestView& v ) { return v.quest->getter(); }, []( QuestView& v, type x ) { v.quest->setter( x ); } )

  static uint8_t checkBitIndex( lua_State* L, int32_t i )
  {
    if( i < 1 || i > 8 )
      luaL_error( L, "bit flag index must be 1..8" );
    return static_cast< uint8_t >( i );
  }

#define QUEST_BITFLAG( n ) \
  .method( "getBitFlag" #n, []( QuestView& v, lua_State* L, int32_t i ) \
  { \
    return v.quest->getBitFlag##n( checkBitIndex( L, i ) ); \
  }, "index" ) \
  .method( "setBitFlag" #n, []( QuestView& v, lua_State* L, int32_t i, bool value ) \
  { \
    v.quest->setBitFlag##n( checkBitIndex( L, i ), value ); \
  }, "index, value" )

  void bindQuest( Bind::Registry& reg )
  {
    Bind::Class< QuestTag >( reg, "Quest", HandleTag::Quest )
      .equality( questEq )
      .prop( "id", []( QuestView& v ) { return v.questId; } )
      .prop( "active", []( QuestView& v ) { return v.player->hasQuest( v.questId ); } )
      QUEST_PROP( "seq", getSeq, setSeq, uint8_t )
      QUEST_PROP( "flags", getFlags, setFlags, uint8_t )
      QUEST_PROP( "ui8A", getUI8A, setUI8A, uint8_t )
      QUEST_PROP( "ui8B", getUI8B, setUI8B, uint8_t )
      QUEST_PROP( "ui8C", getUI8C, setUI8C, uint8_t )
      QUEST_PROP( "ui8D", getUI8D, setUI8D, uint8_t )
      QUEST_PROP( "ui8E", getUI8E, setUI8E, uint8_t )
      QUEST_PROP( "ui8F", getUI8F, setUI8F, uint8_t )
      QUEST_PROP( "ui8AH", getUI8AH, setUI8AH, uint8_t )
      QUEST_PROP( "ui8BH", getUI8BH, setUI8BH, uint8_t )
      QUEST_PROP( "ui8CH", getUI8CH, setUI8CH, uint8_t )
      QUEST_PROP( "ui8DH", getUI8DH, setUI8DH, uint8_t )
      QUEST_PROP( "ui8EH", getUI8EH, setUI8EH, uint8_t )
      QUEST_PROP( "ui8FH", getUI8FH, setUI8FH, uint8_t )
      QUEST_PROP( "ui8AL", getUI8AL, setUI8AL, uint8_t )
      QUEST_PROP( "ui8BL", getUI8BL, setUI8BL, uint8_t )
      QUEST_PROP( "ui8CL", getUI8CL, setUI8CL, uint8_t )
      QUEST_PROP( "ui8DL", getUI8DL, setUI8DL, uint8_t )
      QUEST_PROP( "ui8EL", getUI8EL, setUI8EL, uint8_t )
      QUEST_PROP( "ui8FL", getUI8FL, setUI8FL, uint8_t )
      QUEST_PROP( "ui16A", getUI16A, setUI16A, uint16_t )
      QUEST_PROP( "ui16B", getUI16B, setUI16B, uint16_t )
      QUEST_PROP( "ui16C", getUI16C, setUI16C, uint16_t )
      QUEST_PROP( "ui32A", getUI32A, setUI32A, uint32_t )
      QUEST_BITFLAG( 8 )
      QUEST_BITFLAG( 16 )
      QUEST_BITFLAG( 24 )
      QUEST_BITFLAG( 32 )
      QUEST_BITFLAG( 40 )
      QUEST_BITFLAG( 48 );
  }

#undef QUEST_PROP
#undef QUEST_BITFLAG
}
