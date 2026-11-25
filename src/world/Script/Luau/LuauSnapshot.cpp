#include "LuauSnapshot.h"
#include "LuauRuntime.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <unordered_map>

namespace Sapphire::Scripting::LuauSnapshot
{
  namespace
  {
    constexpr int kCaptureDepth = 32;
    constexpr int kPrintDepth = 8;
    constexpr int kStackPerLevel = 8; // Luau stack slots one recursion level may use; the API does not grow the stack

    std::string formatNumber( double d )
    {
      char buf[ 64 ];
      if( std::floor( d ) == d && std::fabs( d ) < 1e15 )
        std::snprintf( buf, sizeof( buf ), "%.0f", d );
      else
        std::snprintf( buf, sizeof( buf ), "%.14g", d );
      return buf;
    }

    // "a", "a.b", "a[3]"
    std::string childPath( lua_State* L, const std::string& parent, int keyIdx )
    {
      std::string key;
      switch( lua_type( L, keyIdx ) )
      {
        case LUA_TSTRING:
          key = lua_tostring( L, keyIdx );
          return parent.empty() ? key : parent + "." + key;
        case LUA_TNUMBER:
          return parent + "[" + formatNumber( lua_tonumber( L, keyIdx ) ) + "]";
        case LUA_TBOOLEAN:
          return parent + "[" + ( lua_toboolean( L, keyIdx ) ? "true" : "false" ) + "]";
        default:
          return parent + "[" + luaL_typename( L, keyIdx ) + "]";
      }
    }

    using SeenTables = std::unordered_map< const void*, std::shared_ptr< SavedTable > >;

    std::optional< SavedValue > captureValue( lua_State* L, int idx, const std::string& path, int depth,
                                              std::set< const void* >& onPath, SeenTables& seen,
                                              std::vector< std::string >& dropped );

    std::optional< SavedValue > captureTable( lua_State* L, int idx, const std::string& path, int depth,
                                              std::set< const void* >& onPath, SeenTables& seen,
                                              std::vector< std::string >& dropped )
    {
      idx = lua_absindex( L, idx );
      lua_checkstack( L, kStackPerLevel );
      const void* id = lua_topointer( L, idx );
      if( onPath.count( id ) )
      {
        dropped.push_back( ( path.empty() ? std::string( "<root>" ) : path ) + ": <cycle>" );
        return std::nullopt;
      }
      if( depth > kCaptureDepth )
      {
        dropped.push_back( path + ": depth > 32" );
        return std::nullopt;
      }
      // shared subtable already captured: reuse so aliasing survives restore
      if( auto it = seen.find( id ); it != seen.end() )
      {
        SavedValue out;
        out.v = it->second;
        return out;
      }
      if( lua_getmetatable( L, idx ) )
      {
        lua_pop( L, 1 );
        dropped.push_back( ( path.empty() ? std::string( "<root>" ) : path ) + ": metatable" );
      }

      onPath.insert( id );
      auto table = std::make_shared< SavedTable >();
      lua_pushnil( L );
      while( lua_next( L, idx ) != 0 )
      {
        int keyIdx = lua_absindex( L, -2 );
        int valIdx = lua_absindex( L, -1 );
        std::string p = childPath( L, path, keyIdx );
        int kt = lua_type( L, keyIdx );
        if( kt != LUA_TSTRING && kt != LUA_TNUMBER && kt != LUA_TBOOLEAN )
        {
          dropped.push_back( p + ": " + luaL_typename( L, keyIdx ) + " key" );
          lua_pop( L, 1 );
          continue;
        }
        auto value = captureValue( L, valIdx, p, depth + 1, onPath, seen, dropped );
        if( value )
        {
          SavedValue key;
          if( kt == LUA_TSTRING )
          {
            size_t len = 0;
            const char* s = lua_tolstring( L, keyIdx, &len );
            key.v = std::string( s, len );
          }
          else if( kt == LUA_TNUMBER )
            key.v = lua_tonumber( L, keyIdx );
          else
            key.v = lua_toboolean( L, keyIdx ) != 0;
          table->emplace_back( std::move( key ), std::move( *value ) );
        }
        lua_pop( L, 1 );
      }
      onPath.erase( id );
      seen.emplace( id, table );
      SavedValue out;
      out.v = table;
      return out;
    }

    std::optional< SavedValue > captureValue( lua_State* L, int idx, const std::string& path, int depth,
                                              std::set< const void* >& onPath, SeenTables& seen,
                                              std::vector< std::string >& dropped )
    {
      SavedValue out;
      switch( lua_type( L, idx ) )
      {
        case LUA_TNIL:
          return out;
        case LUA_TBOOLEAN:
          out.v = lua_toboolean( L, idx ) != 0;
          return out;
        case LUA_TNUMBER:
          out.v = lua_tonumber( L, idx );
          return out;
        case LUA_TSTRING:
        {
          size_t len = 0;
          const char* s = lua_tolstring( L, idx, &len );
          out.v = std::string( s, len );
          return out;
        }
        case LUA_TVECTOR:
        {
          const float* v = lua_tovector( L, idx );
          out.v = Common::Vector3{ v[ 0 ], v[ 1 ], v[ 2 ] };
          return out;
        }
        case LUA_TTABLE:
          return captureTable( L, idx, path, depth, onPath, seen, dropped );
        default:
          dropped.push_back( path + ": " + luaL_typename( L, idx ) );
          return std::nullopt;
      }
    }

    // cacheIdx: absolute index of a table mapping SavedTable* (light userdata) -> rebuilt table
    void pushValue( lua_State* L, const SavedValue& value, int cacheIdx )
    {
      std::visit( [ L, cacheIdx ]( const auto& v )
      {
        using T = std::decay_t< decltype( v ) >;
        if constexpr( std::is_same_v< T, std::monostate > )
          lua_pushnil( L );
        else if constexpr( std::is_same_v< T, bool > )
          lua_pushboolean( L, v ? 1 : 0 );
        else if constexpr( std::is_same_v< T, double > )
          lua_pushnumber( L, v );
        else if constexpr( std::is_same_v< T, std::string > )
          lua_pushlstring( L, v.data(), v.size() );
        else if constexpr( std::is_same_v< T, Common::Vector3 > )
          lua_pushvector( L, v.x, v.y, v.z );
        else
        {
          if( v )
          {
            lua_pushlightuserdata( L, v.get() );
            lua_rawget( L, cacheIdx );
            if( !lua_isnil( L, -1 ) )
              return;
            lua_pop( L, 1 );
          }
          lua_checkstack( L, kStackPerLevel );
          lua_createtable( L, 0, v ? static_cast< int >( v->size() ) : 0 );
          if( v )
          {
            lua_pushlightuserdata( L, v.get() );
            lua_pushvalue( L, -2 );
            lua_rawset( L, cacheIdx );
            for( auto& [ key, val ] : *v )
            {
              pushValue( L, key, cacheIdx );
              pushValue( L, val, cacheIdx );
              lua_rawset( L, -3 );
            }
          }
        }
      }, value.v );
    }

    void stripTable( lua_State* L, int idx, const std::string& path, int depth, std::set< const void* >& visited,
                     std::vector< std::string >& dropped )
    {
      idx = lua_absindex( L, idx );
      lua_checkstack( L, kStackPerLevel );
      std::string where = path.empty() ? std::string( "<root>" ) : path;
      // only mark visited once actually stripped, so a table first reached past the depth cap can still be stripped
      // through a shorter path
      const void* ptr = lua_topointer( L, idx );
      if( visited.count( ptr ) )
        return;
      if( depth > kCaptureDepth )
      {
        dropped.push_back( where + ": depth > 32, not stripped" );
        return;
      }
      visited.insert( ptr );
      bool readonly = lua_getreadonly( L, idx ) != 0;

      if( lua_getmetatable( L, idx ) )
      {
        lua_pop( L, 1 );
        if( readonly )
          dropped.push_back( where + ": metatable (readonly table, not stripped)" );
        else
        {
          dropped.push_back( where + ": metatable" );
          lua_pushnil( L );
          lua_setmetatable( L, idx );
        }
      }

      lua_newtable( L ); // keys to clear
      int clear = lua_gettop( L );
      int n = 0;
      lua_pushnil( L );
      while( lua_next( L, idx ) != 0 )
      {
        int keyIdx = lua_absindex( L, -2 );
        int kt = lua_type( L, keyIdx );
        int t = lua_type( L, -1 );
        bool keyCode = kt == LUA_TFUNCTION || kt == LUA_TTHREAD;
        bool valCode = t == LUA_TFUNCTION || t == LUA_TTHREAD;
        if( keyCode || valCode )
        {
          std::string p = childPath( L, path, keyIdx );
          std::string what = keyCode ? std::string( luaL_typename( L, keyIdx ) ) + " key" : luaL_typename( L, -1 );
          if( readonly )
            dropped.push_back( p + ": " + what + " (readonly table, not stripped)" );
          else
          {
            dropped.push_back( p + ": " + what );
            lua_pushvalue( L, keyIdx );
            lua_rawseti( L, clear, ++n );
          }
        }
        else if( t == LUA_TTABLE )
          stripTable( L, -1, childPath( L, path, keyIdx ), depth + 1, visited, dropped );
        lua_pop( L, 1 );
      }
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, clear, i );
        lua_pushnil( L );
        lua_rawset( L, idx );
      }
      lua_pop( L, 1 );
    }

    std::string valueText( lua_State* L, int idx )
    {
      switch( lua_type( L, idx ) )
      {
        case LUA_TNIL:
          return "nil";
        case LUA_TBOOLEAN:
          return lua_toboolean( L, idx ) ? "true" : "false";
        case LUA_TNUMBER:
          return formatNumber( lua_tonumber( L, idx ) );
        case LUA_TSTRING:
          return std::string( "\"" ) + lua_tostring( L, idx ) + "\"";
        case LUA_TVECTOR:
        {
          const float* v = lua_tovector( L, idx );
          return "vector(" + formatNumber( v[ 0 ] ) + ", " + formatNumber( v[ 1 ] ) + ", " + formatNumber( v[ 2 ] ) + ")";
        }
        case LUA_TUSERDATA:
        {
          size_t len = 0;
          const char* s = luaL_tolstring( L, idx, &len );
          std::string out( s, len );
          lua_pop( L, 1 );
          return out;
        }
        default:
          return std::string( "<" ) + luaL_typename( L, idx ) + ">";
      }
    }

    void printTable( lua_State* L, int idx, int depth, std::set< const void* >& onPath, std::vector< std::string >& out )
    {
      idx = lua_absindex( L, idx );
      lua_checkstack( L, kStackPerLevel );
      std::string indent( static_cast< size_t >( depth ) * 2, ' ' );

      struct Entry
      {
        int kind; // 0 number, 1 boolean, 2 string, 3 other
        double num;
        std::string text;
        int slot;
      };
      std::vector< Entry > entries;

      lua_newtable( L ); // slot -> key, slot + 1 -> value
      int tmp = lua_gettop( L );
      int slot = 1;
      lua_pushnil( L );
      while( lua_next( L, idx ) != 0 )
      {
        Entry e{ 3, 0, "", slot };
        switch( lua_type( L, -2 ) )
        {
          case LUA_TNUMBER:
            e.kind = 0;
            e.num = lua_tonumber( L, -2 );
            e.text = "[" + formatNumber( e.num ) + "]";
            break;
          case LUA_TBOOLEAN:
            e.kind = 1;
            e.text = lua_toboolean( L, -2 ) ? "[true]" : "[false]";
            break;
          case LUA_TSTRING:
            e.kind = 2;
            e.text = lua_tostring( L, -2 );
            break;
          default:
            e.text = std::string( "[" ) + luaL_typename( L, -2 ) + "]";
            break;
        }
        lua_pushvalue( L, -2 );
        lua_rawseti( L, tmp, slot );
        lua_rawseti( L, tmp, slot + 1 ); // pops the value
        slot += 2;
        entries.push_back( std::move( e ) );
      }

      std::sort( entries.begin(), entries.end(), []( const Entry& a, const Entry& b )
      {
        if( a.kind != b.kind )
          return a.kind < b.kind;
        if( a.kind == 0 )
          return a.num < b.num;
        return a.text < b.text;
      } );

      for( auto& e : entries )
      {
        lua_rawgeti( L, tmp, e.slot + 1 );
        if( lua_type( L, -1 ) == LUA_TTABLE )
        {
          const void* id = lua_topointer( L, -1 );
          if( onPath.count( id ) )
            out.push_back( indent + e.text + " = <cycle>" );
          else if( depth + 1 > kPrintDepth )
            out.push_back( indent + e.text + " = \xE2\x80\xA6" );
          else
          {
            out.push_back( indent + e.text + ":" );
            onPath.insert( id );
            printTable( L, -1, depth + 1, onPath, out );
            onPath.erase( id );
          }
        }
        else
          out.push_back( indent + e.text + " = " + valueText( L, -1 ) );
        lua_pop( L, 1 );
      }
      lua_pop( L, 1 );
    }
  }

  int newTable( LuauRuntime& runtime )
  {
    lua_State* L = runtime.state();
    lua_newtable( L );
    int ref = lua_ref( L, -1 );
    lua_pop( L, 1 );
    return ref;
  }

  void release( LuauRuntime& runtime, int ref )
  {
    if( ref < 0 || !runtime.state() )
      return;
    lua_unref( runtime.state(), ref );
  }

  void stripCode( LuauRuntime& runtime, int ref, std::vector< std::string >& dropped )
  {
    lua_State* L = runtime.state();
    if( !L || ref < 0 )
      return;
    lua_getref( L, ref );
    if( lua_istable( L, -1 ) )
    {
      std::set< const void* > visited;
      stripTable( L, -1, "", 0, visited, dropped );
    }
    lua_pop( L, 1 );
  }

  SavedValue capture( LuauRuntime& runtime, int ref, std::vector< std::string >& dropped )
  {
    lua_State* L = runtime.state();
    if( !L || ref < 0 )
      return {};
    lua_getref( L, ref );
    std::set< const void* > onPath;
    SeenTables seen;
    auto value = captureValue( L, -1, "", 0, onPath, seen, dropped );
    lua_pop( L, 1 );
    return value ? *value : SavedValue{};
  }

  int restore( LuauRuntime& runtime, const SavedValue& value )
  {
    lua_State* L = runtime.state();
    if( std::holds_alternative< std::shared_ptr< SavedTable > >( value.v ) )
    {
      lua_newtable( L ); // SavedTable* -> rebuilt table
      int cache = lua_gettop( L );
      pushValue( L, value, cache );
      lua_remove( L, cache );
    }
    else
      lua_newtable( L );
    int ref = lua_ref( L, -1 );
    lua_pop( L, 1 );
    return ref;
  }

  std::vector< std::string > print( LuauRuntime& runtime, int ref )
  {
    std::vector< std::string > out;
    lua_State* L = runtime.state();
    if( !L || ref < 0 )
      return out;
    lua_getref( L, ref );
    if( lua_istable( L, -1 ) )
    {
      std::set< const void* > onPath{ lua_topointer( L, -1 ) };
      printTable( L, -1, 0, onPath, out );
    }
    lua_pop( L, 1 );
    return out;
  }
}
