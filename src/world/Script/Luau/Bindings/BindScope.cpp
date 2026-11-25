// Scope globals: director, instance, territory, battle, encounter. Each resolves through the calling thread's context:
// ctx.owner -> InstanceRecord -> record.scope.

#include "Handles.h"
#include "../LuauScriptMgr.h"

#include "Actor/BNpc.h"
#include "Actor/EventObject.h"
#include "Actor/Player.h"
#include "Event/Director.h"
#include "Territory/InstanceContent.h"
#include "Territory/QuestBattle.h"
#include "Territory/Territory.h"
#include "Encounter/Encounter.h"
#include "Encounter/TimelinePack.h"

namespace Sapphire::Scripting::Bindings
{
  struct DirectorTag
  {
  };
  struct DirectorVarsTag
  {
  };
  struct InstanceTag
  {
  };
  struct TerritoryTag
  {
  };
  struct BattleTag
  {
  };

  struct ScopeView
  {
    lua_State* L;
    InstanceRecord* record;

    ScriptScope& scope() const { return record->scope; }

    std::shared_ptr< Event::Director > director() const
    {
      if( !record->scope.hasDirector )
        luaL_error( L, "director is not available: this script's scope has no director" );
      auto d = record->scope.director.lock();
      if( !d )
        luaL_error( L, "director: the territory is gone" );
      return d;
    }

    std::shared_ptr< Territory > territory() const
    {
      auto t = record->scope.territory.lock();
      if( !t )
        luaL_error( L, "territory: the territory is gone" );
      return t;
    }

    std::shared_ptr< InstanceContent > instance() const
    {
      if( !record->scope.hasInstance )
        luaL_error( L, "instance is only available in instance content scripts" );
      auto i = record->scope.instance.lock();
      if( !i )
        luaL_error( L, "instance: the territory is gone" );
      return i;
    }

    std::shared_ptr< QuestBattle > battle() const
    {
      if( !record->scope.hasQuestBattle )
        luaL_error( L, "battle is only available in quest battle scripts" );
      auto b = record->scope.questBattle.lock();
      if( !b )
        luaL_error( L, "battle: the territory is gone" );
      return b;
    }
  };

  ScopeView scopeOf( lua_State* L )
  {
    auto* record = luauMgr( L ).currentRecord();
    if( !record )
      luaL_error( L, "no scope bound (top-level code must not touch the world)" );
    return ScopeView{ L, record };
  }

  // director.data: the table at record.dataRef
  struct DataTable
  {
    int ref;
  };

  struct DirectorVarsHandle
  {
  };
}

namespace Sapphire::Scripting::Bind
{
  template< typename Tag >
  struct ScopeTarget
  {
    using Storage = Tag;
    static std::optional< Bindings::ScopeView > lock( lua_State* L, Storage&, const char* )
    {
      return Bindings::scopeOf( L );
    }
  };

  template<>
  struct Target< Bindings::DirectorTag > : ScopeTarget< Bindings::DirectorTag >
  {
  };
  template<>
  struct Target< Bindings::DirectorVarsTag > : ScopeTarget< Bindings::DirectorVarsTag >
  {
  };
  template<>
  struct Target< Bindings::InstanceTag > : ScopeTarget< Bindings::InstanceTag >
  {
  };
  template<>
  struct Target< Bindings::TerritoryTag > : ScopeTarget< Bindings::TerritoryTag >
  {
  };
  template<>
  struct Target< Bindings::BattleTag > : ScopeTarget< Bindings::BattleTag >
  {
  };

  template<>
  struct Stack< Bindings::DataTable >
  {
    static int push( lua_State* L, Bindings::DataTable t )
    {
      if( t.ref < 0 )
        luaL_error( L, "director.data is not available" );
      lua_getref( L, t.ref );
      return 1;
    }
    static std::string luauType() { return "{ [any]: any }"; }
  };

  template<>
  struct Stack< Bindings::DirectorVarsHandle >
  {
    static int push( lua_State* L, Bindings::DirectorVarsHandle )
    {
      Class< Bindings::DirectorVarsTag >::push( L, Bindings::HandleTag::ScopeDirectorVars, {} );
      return 1;
    }
    static std::string luauType() { return "DirectorVars"; }
  };
}

namespace Sapphire::Scripting::Bindings
{
  namespace
  {
    uint8_t checkVarIndex( lua_State* L, int idx )
    {
      if( lua_type( L, idx ) != LUA_TNUMBER )
        luaL_error( L, "director var index out of range" );
      double d = lua_tonumber( L, idx );
      if( d < 0 || d > 9 || d != static_cast< double >( static_cast< int >( d ) ) )
        luaL_error( L, "director var index out of range" );
      return static_cast< uint8_t >( d );
    }

    int l_varsIndex( lua_State* L )
    {
      auto view = scopeOf( L );
      auto index = checkVarIndex( L, 2 );
      lua_pushnumber( L, view.director()->getDirectorVar( index ) );
      return 1;
    }

    int l_varsNewIndex( lua_State* L )
    {
      auto view = scopeOf( L );
      auto index = checkVarIndex( L, 2 );
      auto value = Bind::Stack< uint8_t >::check( L, 3 );
      view.director();
      view.scope().setVar( index, value );
      return 0;
    }

    // reads a field of the table at idx; missing required fields raise "<fn>: missing field '<name>'"
    bool getField( lua_State* L, int idx, const char* name, const char* fn, bool required )
    {
      lua_getfield( L, idx, name );
      if( lua_isnil( L, -1 ) )
      {
        lua_pop( L, 1 );
        if( required )
          luaL_error( L, "%s: missing field '%s'", fn, name );
        return false;
      }
      return true;
    }

    double numField( lua_State* L, int idx, const char* name, const char* fn, bool required, double def = 0 )
    {
      if( !getField( L, idx, name, fn, required ) )
        return def;
      if( lua_type( L, -1 ) != LUA_TNUMBER )
        luaL_error( L, "%s: field '%s' must be a number", fn, name );
      double v = lua_tonumber( L, -1 );
      lua_pop( L, 1 );
      return v;
    }

    Common::Vector3 vecField( lua_State* L, int idx, const char* name, const char* fn, bool required )
    {
      if( !getField( L, idx, name, fn, required ) )
        return {};
      if( lua_type( L, -1 ) != LUA_TVECTOR )
        luaL_error( L, "%s: field '%s' must be a vector", fn, name );
      const float* v = lua_tovector( L, -1 );
      Common::Vector3 out{ v[ 0 ], v[ 1 ], v[ 2 ] };
      lua_pop( L, 1 );
      return out;
    }

    std::string strField( lua_State* L, int idx, const char* name, const char* fn, bool required )
    {
      if( !getField( L, idx, name, fn, required ) )
        return {};
      if( lua_type( L, -1 ) != LUA_TSTRING )
        luaL_error( L, "%s: field '%s' must be a string", fn, name );
      std::string out = lua_tostring( L, -1 );
      lua_pop( L, 1 );
      return out;
    }

    // errors unless the value at idx is a table; element > 0 names a list entry. Returns idx for chaining
    int checkTableAt( lua_State* L, int idx, const char* what, int element = 0 )
    {
      if( !lua_istable( L, idx ) )
      {
        if( element > 0 )
          luaL_error( L, "%s[%d]: expected table, got %s", what, element, luaL_typename( L, idx ) );
        luaL_error( L, "%s: expected table, got %s", what, luaL_typename( L, idx ) );
      }
      return idx;
    }

    // territory:spawnEObj( def ) -> EObj; returns the existing eobj when one with def.name is spawned
    int l_spawnEObj( lua_State* L )
    {
      auto view = scopeOf( L );
      luaL_checktype( L, 2, LUA_TTABLE );
      const char* fn = "spawnEObj";
      auto name = strField( L, 2, "name", fn, true );

      auto territory = view.territory();
      Entity::EventObjectPtr existing;
      if( view.scope().hasInstance )
        existing = view.instance()->getEObjByName( name );
      else
        existing = territory->getEObjByName( name );
      if( existing )
      {
        pushEObj( L, existing );
        return 1;
      }

      auto baseId = static_cast< uint32_t >( numField( L, 2, "baseId", fn, true ) );
      auto boundId = static_cast< uint32_t >( numField( L, 2, "boundId", fn, true ) );
      auto instanceId = static_cast< uint32_t >( numField( L, 2, "instanceId", fn, true ) );
      auto state = static_cast< uint8_t >( numField( L, 2, "state", fn, true ) );
      auto pos = vecField( L, 2, "pos", fn, true );
      auto scale = static_cast< float >( numField( L, 2, "scale", fn, true ) );
      auto rotation = static_cast< float >( numField( L, 2, "rotation", fn, true ) );
      auto invisibility = static_cast< uint8_t >( numField( L, 2, "invisibility", fn, false, 0 ) );

      auto eobj = territory->addEObj( name, baseId, boundId, instanceId, state, pos, scale, rotation, invisibility );
      if( !eobj )
        luaL_error( L, "spawnEObj: could not spawn '%s'", name.c_str() );

      if( getField( L, 2, "collision", fn, false ) )
      {
        int boxes = checkTableAt( L, lua_gettop( L ), "spawnEObj.collision" );
        int n = lua_objlen( L, boxes );
        for( int i = 1; i <= n; ++i )
        {
          lua_rawgeti( L, boxes, i );
          int box = checkTableAt( L, lua_gettop( L ), "spawnEObj.collision", i );
          eobj->addCollisionBox( vecField( L, box, "pos", "spawnEObj.collision", true ),
                                 static_cast< float >( numField( L, box, "rotation", "spawnEObj.collision", true ) ),
                                 static_cast< float >( numField( L, box, "width", "spawnEObj.collision", true ) ),
                                 static_cast< float >( numField( L, box, "height", "spawnEObj.collision", true ) ),
                                 static_cast< float >( numField( L, box, "depth", "spawnEObj.collision", true ) ) );
          lua_pop( L, 1 );
        }
        lua_pop( L, 1 );
      }
      if( getField( L, 2, "collisionEnabled", fn, false ) )
      {
        bool enabled = lua_toboolean( L, -1 ) != 0;
        lua_pop( L, 1 );
        if( enabled )
          eobj->setCollisionEnabled( true );
      }

      pushEObj( L, eobj );
      return 1;
    }

    EncounterEntityRemoveFlag removeFlag( lua_State* L, const std::string& s )
    {
      if( s.empty() || s == "none" )
        return EncounterEntityRemoveFlag::None;
      if( s == "fail" )
        return EncounterEntityRemoveFlag::OnFail;
      if( s == "success" )
        return EncounterEntityRemoveFlag::OnSuccess;
      luaL_error( L, "encounter: unknown remove flag '%s' (none | fail | success)", s.c_str() );
      return EncounterEntityRemoveFlag::None;
    }

    // plain EObjDef tables plus remove = "none" | "fail" | "success"
    std::vector< EncounterEObj > eobjList( lua_State* L, int defIdx, const char* field )
    {
      std::vector< EncounterEObj > out;
      if( !getField( L, defIdx, field, "encounter", false ) )
        return out;
      std::string fn = std::string( "encounter." ) + field;
      int list = checkTableAt( L, lua_gettop( L ), fn.c_str() );
      int n = lua_objlen( L, list );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, list, i );
        int e = checkTableAt( L, lua_gettop( L ), fn.c_str(), i );
        EncounterEObj o{};
        o.name = strField( L, e, "name", fn.c_str(), true );
        o.baseId = static_cast< uint32_t >( numField( L, e, "baseId", fn.c_str(), true ) );
        o.boundInstanceId = static_cast< uint32_t >( numField( L, e, "boundId", fn.c_str(), true ) );
        o.instanceId = static_cast< uint32_t >( numField( L, e, "instanceId", fn.c_str(), true ) );
        o.state = static_cast< uint8_t >( numField( L, e, "state", fn.c_str(), true ) );
        o.pos = vecField( L, e, "pos", fn.c_str(), true );
        o.scale = static_cast< float >( numField( L, e, "scale", fn.c_str(), true ) );
        o.rotation = static_cast< float >( numField( L, e, "rotation", fn.c_str(), true ) );
        o.permissionInvisibility = static_cast< uint8_t >( numField( L, e, "invisibility", fn.c_str(), false, 0 ) );
        o.entityRemoveFlag = removeFlag( L, strField( L, e, "remove", fn.c_str(), false ) );
        out.push_back( std::move( o ) );
        lua_pop( L, 1 );
      }
      lua_pop( L, 1 );
      return out;
    }

    // encounter.start( def ); a no-op when an encounter with def.name was already started in this scope, which makes
    // catch-up after a reload safe
    void startEncounter( lua_State* L, InstanceRecord& record, int def )
    {
      auto name = strField( L, def, "name", "encounter", true );
      if( record.encounters.count( name ) )
        return;

      EncounterSetup setup;
      setup.timelineName = strField( L, def, "timeline", "encounter", true );

      auto shape = strField( L, def, "shape", "encounter", false );
      if( shape.empty() || shape == "polygon" )
        setup.encounterShape = EncounterShape::POLYGON;
      else if( shape == "box" )
        setup.encounterShape = EncounterShape::BOX;
      else if( shape == "cylinder" )
        setup.encounterShape = EncounterShape::CYLINDER;
      else
        luaL_error( L, "encounter: unknown shape '%s' (box | cylinder | polygon)", shape.c_str() );
      setup.polygonShapeFile = strField( L, def, "polygon", "encounter", false );
      if( setup.polygonShapeFile.empty() && setup.encounterShape == EncounterShape::POLYGON )
        setup.polygonShapeFile = setup.timelineName;
      setup.position = vecField( L, def, "position", "encounter", false );
      setup.position2 = vecField( L, def, "position2", "encounter", false );

      std::vector< uint32_t > bosses;
      if( getField( L, def, "bnpcs", "encounter", false ) )
      {
        int list = checkTableAt( L, lua_gettop( L ), "encounter.bnpcs" );
        int n = lua_objlen( L, list );
        for( int i = 1; i <= n; ++i )
        {
          lua_rawgeti( L, list, i );
          int b = checkTableAt( L, lua_gettop( L ), "encounter.bnpcs", i );
          EncounterBNpc bnpc{};
          bnpc.layoutId = static_cast< uint32_t >( numField( L, b, "layoutId", "encounter.bnpcs", true ) );
          bnpc.hp = static_cast< uint32_t >( numField( L, b, "hp", "encounter.bnpcs", true ) );
          auto type = strField( L, b, "type", "encounter.bnpcs", false );
          if( type.empty() || type == "enemy" )
            bnpc.type = Common::BNpcType::Enemy;
          else if( type == "friendly" )
            bnpc.type = Common::BNpcType::Friendly;
          else
            luaL_error( L, "encounter: unknown bnpc type '%s' (enemy | friendly)", type.c_str() );
          if( getField( L, b, "flags", "encounter.bnpcs", false ) )
          {
            bnpc.flags = Bind::Stack< Bind::Flags >::check( L, lua_gettop( L ) ).value;
            lua_pop( L, 1 );
          }
          if( getField( L, b, "boss", "encounter.bnpcs", false ) )
          {
            bnpc.isBoss = lua_toboolean( L, -1 ) != 0;
            lua_pop( L, 1 );
          }
          if( bnpc.isBoss )
            bosses.push_back( bnpc.layoutId );
          setup.bnpcSetupList.push_back( bnpc );
          lua_pop( L, 1 );
        }
        lua_pop( L, 1 );
      }

      setup.onInitEObjSetupList = eobjList( L, def, "initEObjs" );
      setup.onSuccessEObjSetupList = eobjList( L, def, "successEObjs" );
      setup.lockoutEntrances = eobjList( L, def, "lockoutEntrances" );
      setup.lockoutExits = eobjList( L, def, "lockoutExits" );
      if( getField( L, def, "lockout", "encounter", false ) )
      {
        setup.hasLockout = lua_toboolean( L, -1 ) != 0;
        lua_pop( L, 1 );
      }
      setup.placeName = static_cast< uint32_t >( numField( L, def, "placeName", "encounter", false, 0 ) );
      setup.bgmInCombat = static_cast< uint16_t >( numField( L, def, "bgmCombat", "encounter", false, 0 ) );
      setup.bgmToRestore = static_cast< uint16_t >( numField( L, def, "bgmRestore", "encounter", false, 0 ) );
      setup.bgmOnFinishTeri = static_cast< uint16_t >( numField( L, def, "bgmFinish", "encounter", false, 0 ) );

      auto territory = record.scope.territory.lock();
      auto director = record.scope.director.lock();
      if( !territory || !director )
        luaL_error( L, "encounter needs a territory and a director" );

      // validate everything Encounter::init dereferences before anything is committed, so a script mistake raises a
      // Luau error instead of leaving a broken encounter that crashes on the next tick
      std::string loadError;
      std::shared_ptr< TimelinePack > timeline;
      try
      {
        timeline = TimelinePack::createTimelinePack( setup.timelineName );
        if( !timeline )
          loadError = "encounter: unknown timeline '" + setup.timelineName + "'";
      }
      catch( const std::exception& e )
      {
        loadError = "encounter: failed to load timeline '" + setup.timelineName + "': " + e.what();
      }
      if( !loadError.empty() )
        luaL_error( L, "%s", loadError.c_str() );
      for( const auto& bnpc : setup.bnpcSetupList )
      {
        if( !territory->getActiveBNpcByLayoutId( bnpc.layoutId ) && !territory->hasBNpcLayout( bnpc.layoutId ) )
          luaL_error( L, "encounter: unknown bnpc layout id %d", static_cast< int >( bnpc.layoutId ) );
      }

      auto encounter = std::make_shared< Encounter >( territory, director, setup.timelineName );
      encounter->setEncounterSetup( setup );
      try
      {
        if( !encounter->init( std::move( timeline ) ) )
          loadError = "encounter: init failed: could not load encounter shape '" + setup.polygonShapeFile + "'";
      }
      catch( const std::exception& e )
      {
        loadError = std::string( "encounter: init failed: " ) + e.what();
      }
      if( !loadError.empty() )
        luaL_error( L, "%s", loadError.c_str() );

      record.encounter = encounter;
      // InstanceContent::updateState dereferences its own encounter pointer on duty reset
      if( auto instance = record.scope.instance.lock() )
        instance->setEncounter( record.encounter );

      record.encounters[ name ] = ScriptEncounterState::Idle;
      record.currentEncounter = name;
      record.currentState = ScriptEncounterState::Idle;
      record.bossLayoutIds = bosses;
      luauMgr( L ).notifyChange( record );
    }

    int l_encounterStart( lua_State* L )
    {
      auto view = scopeOf( L );
      if( !view.scope().hasDirector )
        luaL_error( L, "encounter needs a territory and a director" );
      view.territory();
      view.director();
      luaL_checktype( L, 1, LUA_TTABLE );
      // an EncounterRef carries its definition in .def
      lua_rawgetfield( L, 1, "def" );
      if( lua_istable( L, -1 ) )
        lua_replace( L, 1 );
      else
        lua_pop( L, 1 );
      startEncounter( L, *view.record, 1 );
      return 0;
    }

    void sendLogMessage( ScopeView& v, Entity::PlayerPtr player, uint32_t msgId, const std::vector< uint32_t >& p )
    {
      auto instance = v.instance();
      auto director = v.director();
      switch( p.size() )
      {
        case 0:
          director->sendEventLogMessage( *player, *instance, msgId );
          break;
        case 1:
          director->sendEventLogMessage( *player, *instance, msgId, { p[ 0 ] } );
          break;
        case 2:
          director->sendEventLogMessage( *player, *instance, msgId, { p[ 0 ], p[ 1 ] } );
          break;
        case 3:
          director->sendEventLogMessage( *player, *instance, msgId, { p[ 0 ], p[ 1 ], p[ 2 ] } );
          break;
        case 4:
          director->sendEventLogMessage( *player, *instance, msgId, { p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] } );
          break;
        default:
          luaL_error( v.L, "at most 4 params" );
      }
    }
  }

  void bindScope( Bind::Registry& reg )
  {
    Bind::Class< DirectorVarsTag >( reg, "DirectorVars", HandleTag::ScopeDirectorVars )
      .rawIndex( l_varsIndex, l_varsNewIndex, "[number]: number" );

    Bind::Class< DirectorTag >( reg, "Director", HandleTag::ScopeDirector )
      .prop( "id", []( ScopeView& v ) { return v.director()->getDirectorId(); } )
      .prop( "seq", []( ScopeView& v ) { return v.director()->getSequence(); },
             []( ScopeView& v, uint8_t s ) { v.director(); v.scope().setSequence( s ); } )
      .prop( "flags", []( ScopeView& v ) { return v.director()->getFlags(); },
             []( ScopeView& v, uint8_t f ) { v.director(); v.scope().setFlags( f ); } )
      .prop( "vars", []( ScopeView& v ) { v.director(); return DirectorVarsHandle{}; } )
      .prop( "data", []( ScopeView& v ) { return DataTable{ v.record->dataRef }; },
             []( ScopeView& v, Bind::LuaRef r )
             {
               lua_unref( v.L, r.ref );
               luaL_error( v.L, "director.data cannot be replaced; assign its fields" );
             } );

    Bind::Class< InstanceTag >( reg, "Instance", HandleTag::ScopeInstance )
      .prop( "guid", []( ScopeView& v ) { return v.instance()->getGuId(); } )
      .prop( "contentId", []( ScopeView& v ) { return v.instance()->getInstanceContentId(); } )
      .prop( "state", []( ScopeView& v ) { return static_cast< uint32_t >( v.instance()->getState() ); } )
      .method( "setState", []( ScopeView& v, uint32_t s )
      {
        v.instance()->setState( static_cast< InstanceContentState >( s ) );
      }, "state" )
      .method( "logMessage", []( ScopeView& v, Entity::PlayerPtr player, uint32_t msgId,
                                 std::optional< std::vector< uint32_t > > params )
      {
        sendLogMessage( v, std::move( player ), msgId, params.value_or( std::vector< uint32_t >{} ) );
      }, "player, msgId, params" )
      .method( "dutyComplete", []( ScopeView& v )
      {
        auto instance = v.instance();
        if( v.record->dutyCompleted )
          return;
        v.record->dutyCompleted = true;
        instance->sendDutyComplete();
      } )
      .method( "setBgm", []( ScopeView& v, uint16_t id ) { v.instance()->setCurrentBGM( id ); }, "id" )
      .method( "players", []( ScopeView& v )
      {
        std::vector< Entity::PlayerPtr > out;
        for( auto& [ id, player ] : v.instance()->getPlayers() )
          if( player )
            out.push_back( player );
        return out;
      } );

    Bind::Class< TerritoryTag >( reg, "Territory", HandleTag::ScopeTerritory )
      .prop( "guid", []( ScopeView& v ) { return v.territory()->getGuId(); } )
      .prop( "typeId", []( ScopeView& v ) { return v.territory()->getTerritoryTypeId(); } )
      .rawMethod( "spawnEObj", l_spawnEObj, "(self, def: EObjDef) -> EObj" )
      .method( "eobj", []( ScopeView& v, std::string name ) -> std::optional< Entity::EventObjectPtr >
      {
        if( v.scope().hasInstance )
          return nullable( v.instance()->getEObjByName( name ) );
        return nullable( v.territory()->getEObjByName( name ) );
      }, "name" )
      .method( "spawnBNpc", []( ScopeView& v, uint32_t layoutId, uint32_t hp, std::optional< std::string > kind )
      {
        auto territory = v.territory();
        if( auto existing = territory->getActiveBNpcByLayoutId( layoutId ) )
          return existing;
        auto type = Common::BNpcType::Enemy;
        if( kind && *kind == "friendly" )
          type = Common::BNpcType::Friendly;
        else if( kind && *kind != "enemy" )
          luaL_error( v.L, "spawnBNpc: kind must be \"enemy\" or \"friendly\"" );
        auto bnpc = territory->createBNpcFromLayoutId( layoutId, hp, type );
        if( !bnpc )
          luaL_error( v.L, "spawnBNpc: unknown layout id %d", static_cast< int >( layoutId ) );
        return bnpc;
      }, "layoutId, hp, kind" )
      .method( "bnpc", []( ScopeView& v, uint32_t layoutId )
      {
        return nullable( v.territory()->getActiveBNpcByLayoutId( layoutId ) );
      }, "layoutId" );

    Bind::Class< BattleTag >( reg, "Battle", HandleTag::ScopeBattle )
      .prop( "questId", []( ScopeView& v ) { return v.battle()->getQuestId(); } )
      .prop( "player", []( ScopeView& v ) { return nullable( v.battle()->getPlayerPtr() ); } )
      .prop( "enemyCount", []( ScopeView& v ) { return v.battle()->getCountEnemyBNpc(); } )
      .method( "success", []( ScopeView& v ) { v.battle()->success(); } )
      .method( "fail", []( ScopeView& v ) { v.battle()->fail(); } );

    Bind::Library( reg, "encounter" )
      .rawFunction( "start", l_encounterStart, "(def: EncounterDef | EncounterRef) -> ()" )
      .function( "status", []( lua_State* L, std::string name ) -> const char*
      {
        auto view = scopeOf( L );
        auto it = view.record->encounters.find( name );
        return it == view.record->encounters.end() ? "none" : scriptEncounterStateName( it->second );
      }, "name" )
      .function( "current", []( lua_State* L ) -> std::optional< std::string >
      {
        auto view = scopeOf( L );
        if( view.record->currentEncounter.empty() )
          return std::nullopt;
        return view.record->currentEncounter;
      } );

    reg.global( "director", "Director", []( lua_State* L )
    {
      Bind::Class< DirectorTag >::push( L, HandleTag::ScopeDirector, {} );
    } );
    reg.global( "instance", "Instance", []( lua_State* L )
    {
      Bind::Class< InstanceTag >::push( L, HandleTag::ScopeInstance, {} );
    } );
    reg.global( "territory", "Territory", []( lua_State* L )
    {
      Bind::Class< TerritoryTag >::push( L, HandleTag::ScopeTerritory, {} );
    } );
    reg.global( "battle", "Battle", []( lua_State* L )
    {
      Bind::Class< BattleTag >::push( L, HandleTag::ScopeBattle, {} );
    } );
  }
}
