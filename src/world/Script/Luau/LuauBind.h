#pragma once

// Binding layer between C++ and Luau. One declaration (Class::prop/method, Library::function, Enum::value, ...)
// yields the runtime registration, the argument marshalling and the Luau type signature that ends up in
// types/sapphire.d.luau. Header-only, C++17, depends only on Luau and the standard library.

#include <lua.h>
#include <lualib.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Sapphire::Scripting::Bind
{
  // A flags argument: a number or an array of numbers that are ORed together.
  struct Flags
  {
    uint32_t value = 0;
  };

  // Any Luau value kept by the host (registry reference). The receiver owns the reference.
  struct LuaRef
  {
    int ref = LUA_NOREF;
  };

  template< typename T >
  using Plain = std::remove_cv_t< std::remove_reference_t< T > >;

  //////////////////////////////////////////////////////////////////////////////////////////////// Stack

  // static int push( lua_State*, const T& ) -> number of pushed values
  // static T check( lua_State*, int idx )
  // static std::string luauType()
  template< typename T, typename = void >
  struct Stack;

  template<>
  struct Stack< bool >
  {
    static int push( lua_State* L, bool v ) { lua_pushboolean( L, v ? 1 : 0 ); return 1; }
    static bool check( lua_State* L, int idx ) { return luaL_checkboolean( L, idx ) != 0; }
    static std::string luauType() { return "boolean"; }
  };

  template< typename T >
  struct Stack< T, std::enable_if_t< std::is_arithmetic_v< T > && !std::is_same_v< T, bool > > >
  {
    static int push( lua_State* L, T v )
    {
      if constexpr( std::is_integral_v< T > && sizeof( T ) == 8 )
      {
        constexpr T kExactInt = T( 1 ) << 53;
        bool tooBig = v > kExactInt;
        if constexpr( std::is_signed_v< T > )
          tooBig = tooBig || v < -kExactInt;
        if( tooBig )
        {
          auto s = std::to_string( v );
          lua_pushlstring( L, s.data(), s.size() );
          return 1;
        }
      }
      lua_pushnumber( L, static_cast< double >( v ) );
      return 1;
    }

    static T check( lua_State* L, int idx )
    {
      if constexpr( std::is_integral_v< T > && sizeof( T ) == 8 )
      {
        if( lua_type( L, idx ) == LUA_TSTRING )
        {
          const char* s = lua_tostring( L, idx );
          char* end = nullptr;
          T v;
          if constexpr( std::is_signed_v< T > )
            v = static_cast< T >( std::strtoll( s, &end, 10 ) );
          else
            v = static_cast< T >( std::strtoull( s, &end, 10 ) );
          if( end == s || *end != '\0' )
            luaL_error( L, "bad argument #%d (expected an integer or a decimal string, got '%s')", idx, s );
          return v;
        }
      }
      double d = luaL_checknumber( L, idx );
      if constexpr( std::is_integral_v< T > )
      {
        // fractions truncate toward zero; NaN and values outside T raise instead of wrapping
        constexpr double lo = static_cast< double >( std::numeric_limits< T >::min() );
        // max() + 1: a power of two, exact as a double for every width
        constexpr double hi = static_cast< double >( std::numeric_limits< T >::max() / 2 + 1 ) * 2.0;
        double t = std::trunc( d );
        if( !( t >= lo && t < hi ) )
          luaL_error( L, "bad argument #%d (%.14g is out of range %.14g..%.14g)", idx, d, lo, hi - 1.0 );
        return static_cast< T >( t );
      }
      else
        return static_cast< T >( d );
    }

    static std::string luauType() { return "number"; }
  };

  template< typename T >
  struct Stack< T, std::enable_if_t< std::is_enum_v< T > > >
  {
    using U = std::underlying_type_t< T >;
    static int push( lua_State* L, T v ) { return Stack< U >::push( L, static_cast< U >( v ) ); }
    static T check( lua_State* L, int idx ) { return static_cast< T >( Stack< U >::check( L, idx ) ); }
    static std::string luauType() { return "number"; }
  };

  template<>
  struct Stack< std::string >
  {
    static int push( lua_State* L, const std::string& v ) { lua_pushlstring( L, v.data(), v.size() ); return 1; }
    static std::string check( lua_State* L, int idx )
    {
      size_t len = 0;
      const char* s = luaL_checklstring( L, idx, &len );
      return std::string( s, len );
    }
    static std::string luauType() { return "string"; }
  };

  template<>
  struct Stack< const char* >
  {
    static int push( lua_State* L, const char* v )
    {
      if( v )
        lua_pushstring( L, v );
      else
        lua_pushnil( L );
      return 1;
    }
    static const char* check( lua_State* L, int idx ) { return luaL_checkstring( L, idx ); }
    static std::string luauType() { return "string"; }
  };

  template<>
  struct Stack< Flags >
  {
    static int push( lua_State* L, Flags v ) { lua_pushnumber( L, v.value ); return 1; }
    static Flags check( lua_State* L, int idx )
    {
      if( lua_type( L, idx ) == LUA_TNUMBER )
        return { static_cast< uint32_t >( static_cast< int64_t >( lua_tonumber( L, idx ) ) ) };
      if( lua_type( L, idx ) != LUA_TTABLE )
        luaL_typeerror( L, idx, "number or array of numbers" );
      Flags out;
      int n = lua_objlen( L, idx );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, idx, i );
        if( lua_type( L, -1 ) != LUA_TNUMBER )
          luaL_error( L, "bad argument #%d (flag list entry %d is not a number)", idx, i );
        out.value |= static_cast< uint32_t >( static_cast< int64_t >( lua_tonumber( L, -1 ) ) );
        lua_pop( L, 1 );
      }
      return out;
    }
    static std::string luauType() { return "number | {number}"; }
  };

  template<>
  struct Stack< LuaRef >
  {
    static int push( lua_State* L, LuaRef v ) { lua_getref( L, v.ref ); return 1; }
    static LuaRef check( lua_State* L, int idx ) { return { lua_ref( L, idx ) }; }
    static std::string luauType() { return "any"; }
  };

  template< typename T >
  struct Stack< std::optional< T > >
  {
    static int push( lua_State* L, const std::optional< T >& v )
    {
      if( !v )
      {
        lua_pushnil( L );
        return 1;
      }
      return Stack< T >::push( L, *v );
    }
    static std::optional< T > check( lua_State* L, int idx )
    {
      if( lua_isnoneornil( L, idx ) )
        return std::nullopt;
      return Stack< T >::check( L, idx );
    }
    static std::string luauType()
    {
      auto t = Stack< T >::luauType();
      if( t.find( '|' ) != std::string::npos || t.find( "->" ) != std::string::npos )
        return "(" + t + ")?";
      return t + "?";
    }
  };

  template< typename T >
  struct Stack< std::vector< T > >
  {
    static int push( lua_State* L, const std::vector< T >& v )
    {
      lua_createtable( L, static_cast< int >( v.size() ), 0 );
      int i = 1;
      for( const auto& e : v )
      {
        if( Stack< T >::push( L, e ) != 1 )
          luaL_error( L, "array element pushed more than one value" );
        lua_rawseti( L, -2, i++ );
      }
      return 1;
    }
    static std::vector< T > check( lua_State* L, int idx )
    {
      idx = lua_absindex( L, idx );
      luaL_checktype( L, idx, LUA_TTABLE );
      std::vector< T > out;
      int n = lua_objlen( L, idx );
      out.reserve( static_cast< size_t >( n ) );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, idx, i );
        out.push_back( Stack< T >::check( L, lua_gettop( L ) ) );
        lua_pop( L, 1 );
      }
      return out;
    }
    static std::string luauType() { return "{" + Stack< T >::luauType() + "}"; }
  };

  template< typename A, typename B >
  struct Stack< std::pair< A, B > >
  {
    static int push( lua_State* L, const std::pair< A, B >& v )
    {
      int n = Stack< A >::push( L, v.first );
      return n + Stack< B >::push( L, v.second );
    }
    static std::string luauType() { return "(" + Stack< A >::luauType() + ", " + Stack< B >::luauType() + ")"; }
  };

  //////////////////////////////////////////////////////////////////////////////////////////////// targets

  // How a userdata payload becomes the object passed to member callables. Storage is what the userdata holds;
  // lock() returns something dereferenceable to the callable's first parameter, or raises a Luau error.
  template< typename P, typename = void >
  struct Target
  {
    using Storage = P;
    static P* lock( lua_State*, Storage& s, const char* ) { return &s; }
  };

  template< typename T >
  struct Target< std::weak_ptr< T > >
  {
    using Storage = std::weak_ptr< T >;
    static std::shared_ptr< T > lock( lua_State* L, Storage& s, const char* typeName )
    {
      auto p = s.lock();
      if( !p )
        luaL_error( L, "%s is no longer valid", typeName );
      return p;
    }
  };

  //////////////////////////////////////////////////////////////////////////////////////////////// traits

  template< typename F >
  struct FnTraits : FnTraits< decltype( &F::operator() ) >
  {
  };

  template< typename C, typename R, typename... A >
  struct FnTraits< R ( C::* )( A... ) const >
  {
    using Ret = R;
    using Args = std::tuple< A... >;
  };

  template< typename C, typename R, typename... A >
  struct FnTraits< R ( C::* )( A... ) >
  {
    using Ret = R;
    using Args = std::tuple< A... >;
  };

  template< typename R, typename... A >
  struct FnTraits< R ( * )( A... ) >
  {
    using Ret = R;
    using Args = std::tuple< A... >;
  };

  namespace detail
  {
    inline std::vector< std::string > splitParams( const char* params )
    {
      std::vector< std::string > out;
      std::string cur;
      for( const char* p = params ? params : ""; ; ++p )
      {
        if( *p == ',' || *p == '\0' )
        {
          size_t b = cur.find_first_not_of( " \t" );
          size_t e = cur.find_last_not_of( " \t" );
          out.push_back( b == std::string::npos ? std::string() : cur.substr( b, e - b + 1 ) );
          cur.clear();
          if( *p == '\0' )
            break;
        }
        else
          cur += *p;
      }
      if( out.size() == 1 && out[ 0 ].empty() )
        out.clear();
      return out;
    }

    template< typename T >
    constexpr bool isState = std::is_same_v< Plain< T >, lua_State* >;

    template< typename Tuple >
    struct TupleTail;

    template< typename H, typename... T >
    struct TupleTail< std::tuple< H, T... > >
    {
      using Head = H;
      using Tail = std::tuple< T... >;
    };

    template<>
    struct TupleTail< std::tuple<> >
    {
      using Head = void;
      using Tail = std::tuple<>;
    };

    // Luau parameter list ("a: number, b: string?") for the argument types in Tuple; lua_State* is skipped.
    template< typename Tuple >
    struct ParamTypes;

    template< typename... A >
    struct ParamTypes< std::tuple< A... > >
    {
      static std::vector< std::string > get()
      {
        std::vector< std::string > out;
        ( [ & ]
        {
          if constexpr( !isState< A > )
            out.push_back( Stack< Plain< A > >::luauType() );
        }(), ... );
        return out;
      }
    };

    inline std::string joinParams( const std::vector< std::string >& types, const char* names, bool self )
    {
      auto n = splitParams( names );
      std::string out = self ? "self" : "";
      for( size_t i = 0; i < types.size(); ++i )
      {
        if( !out.empty() )
          out += ", ";
        std::string name = i < n.size() && !n[ i ].empty() ? n[ i ] : "arg" + std::to_string( i + 1 );
        out += name + ": " + types[ i ];
      }
      return out;
    }

    template< typename R >
    std::string retType()
    {
      if constexpr( std::is_void_v< R > )
        return "()";
      else
        return Stack< Plain< R > >::luauType();
    }

    // "(self, a: number) -> boolean" -> { "self, a: number", "boolean" }
    inline std::pair< std::string, std::string > parseSignature( const std::string& sig )
    {
      size_t open = sig.find( '(' );
      if( open == std::string::npos )
        return { "", "()" };
      int depth = 0;
      size_t close = std::string::npos;
      for( size_t i = open; i < sig.size(); ++i )
      {
        if( sig[ i ] == '(' || sig[ i ] == '{' )
          ++depth;
        else if( sig[ i ] == ')' || sig[ i ] == '}' )
        {
          if( --depth == 0 )
          {
            close = i;
            break;
          }
        }
      }
      if( close == std::string::npos )
        return { sig.substr( open + 1 ), "()" };
      std::string params = sig.substr( open + 1, close - open - 1 );
      std::string rest = sig.substr( close + 1 );
      size_t arrow = rest.find( "->" );
      std::string ret = "()";
      if( arrow != std::string::npos )
      {
        ret = rest.substr( arrow + 2 );
        size_t b = ret.find_first_not_of( " \t" );
        size_t e = ret.find_last_not_of( " \t" );
        ret = b == std::string::npos ? "()" : ret.substr( b, e - b + 1 );
      }
      return { params, ret };
    }

    // Reads the arguments for Tuple from the stack starting at `first`. A leading lua_State* receives L.
    template< typename Tuple >
    struct Reader;

    template< typename... A >
    struct Reader< std::tuple< A... > >
    {
      template< size_t... I >
      static auto read( lua_State* L, int first, std::index_sequence< I... > )
      {
        return std::tuple< Plain< A >... >{ Stack< Plain< A > >::check( L, first + static_cast< int >( I ) )... };
      }

      static auto read( lua_State* L, int first )
      {
        return read( L, first, std::index_sequence_for< A... >{} );
      }
    };

    template< typename R, typename Fn, typename Args >
    int callAndPush( lua_State* L, Fn&& invoke, Args& args )
    {
      if constexpr( std::is_void_v< R > )
      {
        std::apply( invoke, args );
        return 0;
      }
      else
      {
        return Stack< Plain< R > >::push( L, std::apply( invoke, args ) );
      }
    }

    // Free callable: arguments start at stack index `first`.
    template< typename F >
    int invokeFree( lua_State* L, F& f, int first )
    {
      using T = FnTraits< F >;
      using R = typename T::Ret;
      using Args = typename T::Args;
      if constexpr( std::tuple_size_v< Args > > 0 && isState< typename TupleTail< Args >::Head > )
      {
        auto args = Reader< typename TupleTail< Args >::Tail >::read( L, first );
        return callAndPush< R >( L, [ & ]( auto&... a ) -> decltype( auto ) { return f( L, a... ); }, args );
      }
      else
      {
        auto args = Reader< Args >::read( L, first );
        return callAndPush< R >( L, [ & ]( auto&... a ) -> decltype( auto ) { return f( a... ); }, args );
      }
    }

    // Member callable: first parameter is the target, then optionally lua_State*, then the Luau arguments.
    template< typename F, typename Tgt >
    int invokeMember( lua_State* L, F& f, Tgt& target, int first )
    {
      using T = FnTraits< F >;
      using R = typename T::Ret;
      using Rest = typename TupleTail< typename T::Args >::Tail;
      if constexpr( std::tuple_size_v< Rest > > 0 && isState< typename TupleTail< Rest >::Head > )
      {
        auto args = Reader< typename TupleTail< Rest >::Tail >::read( L, first );
        return callAndPush< R >( L, [ & ]( auto&... a ) -> decltype( auto ) { return f( target, L, a... ); }, args );
      }
      else
      {
        auto args = Reader< Rest >::read( L, first );
        return callAndPush< R >( L, [ & ]( auto&... a ) -> decltype( auto ) { return f( target, a... ); }, args );
      }
    }

    template< typename F >
    void pushFunctor( lua_State* L, F f )
    {
      void* mem = lua_newuserdatadtor( L, sizeof( F ), []( lua_State*, void* p ) { static_cast< F* >( p )->~F(); } );
      new( mem ) F( std::move( f ) );
    }

    template< typename F >
    F& upFunctor( lua_State* L )
    {
      return *static_cast< F* >( lua_touserdata( L, lua_upvalueindex( 1 ) ) );
    }

    inline void* checkSelf( lua_State* L, int idx, uint64_t mask, const char* className )
    {
      int tag = lua_userdatatag( L, idx );
      if( tag < 0 || tag >= 64 || !( mask & ( uint64_t( 1 ) << tag ) ) )
        luaL_error( L, "expected a %s as self (call methods with ':')", className );
      return lua_touserdata( L, idx );
    }

    // A member call: self at stack index 1, the arguments from 2. Entered from the member's closure (memberThunk)
    // and, for methods, directly from __namecall.
    template< typename P, typename F >
    int memberInvoke( lua_State* L, void* functor, uint64_t mask, const char* className )
    {
      auto* s = static_cast< typename Target< P >::Storage* >( checkSelf( L, 1, mask, className ) );
      auto&& holder = Target< P >::lock( L, *s, className );
      return invokeMember( L, *static_cast< F* >( functor ), *holder, 2 );
    }

    template< typename P, typename F >
    int memberThunk( lua_State* L )
    {
      auto mask = static_cast< uint64_t >( reinterpret_cast< uintptr_t >( lua_tolightuserdata( L, lua_upvalueindex( 2 ) ) ) );
      return memberInvoke< P, F >( L, &upFunctor< F >( L ), mask, lua_tostring( L, lua_upvalueindex( 3 ) ) );
    }

    // Method names as Luau string atoms (lua_namecallatom), so that __namecall finds a method without hashing its
    // name. Process-wide and append-only: every VM and Registry numbers a name the same way.
    inline std::unordered_map< std::string, int16_t >& atomTable()
    {
      static std::unordered_map< std::string, int16_t > table;
      return table;
    }

    // -1 when the name has no atom
    inline int16_t findAtom( const char* s, size_t l )
    {
      auto& table = atomTable();
      auto it = table.find( std::string( s, l ) );
      return it == table.end() ? int16_t( -1 ) : it->second;
    }

    inline int16_t internAtom( const std::string& name )
    {
      auto& table = atomTable();
      auto it = table.find( name );
      if( it != table.end() )
        return it->second;
      if( table.size() >= static_cast< size_t >( std::numeric_limits< int16_t >::max() ) )
        return -1;
      auto atom = static_cast< int16_t >( table.size() );
      table.emplace( name, atom );
      return atom;
    }

    // lua_Callbacks::useratom
    inline int16_t userAtom( lua_State*, const char* s, size_t l )
    {
      return findAtom( s, l );
    }

    template< typename F >
    int freeThunk( lua_State* L )
    {
      return invokeFree( L, upFunctor< F >( L ), 1 );
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////// registry

  class Registry;

  enum class MemberKind : uint8_t
  {
    Method,   // native, signature generated
    Raw,      // native lua_CFunction, signature verbatim
    Luau,     // implemented in common.luau, declaration only
    Prop
  };

  // pushes a C closure; argument = accepted tag mask (members only)
  using ClosurePusher = std::function< void( lua_State*, uint64_t mask ) >;

  struct MemberDecl
  {
    MemberKind kind;
    std::string name;
    std::string params; // without parentheses; includes "self" for members
    std::string ret;    // return type, or the property type
    ClosurePusher push;
    ClosurePusher pushSetter;
    // methods of a class: what __namecall calls directly, see detail::memberInvoke
    int ( *invoke )( lua_State*, void* functor, uint64_t mask, const char* className ) = nullptr;
    std::shared_ptr< void > functor;
    bool writable = false;
    bool value = false; // Luau library member that is a value of type `ret`, not a function
  };

  struct ClassDecl
  {
    std::string name;
    int tag = -1;
    std::string base;
    std::vector< MemberDecl > members;
    lua_Destructor dtor = nullptr;
    lua_CFunction eq = nullptr;
    lua_CFunction rawIndex = nullptr;    // (self, key) for keys that are no property or method
    lua_CFunction rawNewIndex = nullptr; // (self, key, value)
    std::string rawIndexType;            // extra declaration line for the raw index, e.g. "[number]: number"
  };

  struct LibraryDecl
  {
    std::string name;
    bool host = false;
    std::vector< MemberDecl > functions;
  };

  struct EnumDecl
  {
    std::string name;
    std::vector< std::pair< std::string, double > > numbers;
    std::vector< std::pair< std::string, std::string > > strings;
  };

  struct GlobalDecl
  {
    std::string name;
    std::string type;
    std::function< void( lua_State* ) > push; // may be empty (declaration only)
  };

  class Registry
  {
  public:
    std::vector< ClassDecl > classes;
    std::vector< LibraryDecl > libraries;
    std::vector< EnumDecl > enums;
    std::vector< GlobalDecl > globals;
    std::vector< std::pair< std::string, std::string > > globalFunctions; // name, signature "(params) -> ret"
    std::vector< std::pair< std::string, std::string > > aliases;
    std::vector< std::pair< std::string, std::string > > luauClasses;     // name, body (declared only)
    std::vector< std::pair< std::string, std::vector< std::pair< std::string, std::string > > > > handlerSets;
    std::string headerComment;

    ClassDecl& classDecl( const std::string& name )
    {
      for( auto& c : classes )
        if( c.name == name )
          return c;
      classes.push_back( ClassDecl{} );
      classes.back().name = name;
      return classes.back();
    }

    LibraryDecl& libraryDecl( const std::string& name, bool host )
    {
      for( auto& l : libraries )
        if( l.name == name && l.host == host )
          return l;
      libraries.push_back( LibraryDecl{} );
      libraries.back().name = name;
      libraries.back().host = host;
      return libraries.back();
    }

    // singleton userdata globals: director, territory, instance, battle
    void global( const char* name, const char* luauType, std::function< void( lua_State* ) > push = {} )
    {
      globals.push_back( { name, luauType, std::move( push ) } );
    }

    // global functions implemented in common.luau: once, eobjDef, encounterDef
    void globalFunction( const char* name, const char* signature )
    {
      globalFunctions.emplace_back( name, signature );
    }

    void typeAlias( const char* name, const char* luauType ) { aliases.emplace_back( name, luauType ); }

    // a class implemented in Luau (declaration only); body = the lines between "declare extern type X with" and "end"
    void luauClass( const char* nameAndBase, const char* body ) { luauClasses.emplace_back( nameAndBase, body ); }

    void handlers( const char* kindName,
                   std::initializer_list< std::pair< const char*, const char* > > nameAndSignature )
    {
      std::vector< std::pair< std::string, std::string > > list;
      for( auto& [ n, s ] : nameAndSignature )
        list.emplace_back( n, s );
      handlerSets.emplace_back( kindName, std::move( list ) );
    }

    bool isHandler( const char* kindName, const char* name ) const
    {
      for( auto& [ kind, list ] : handlerSets )
        if( kind == kindName )
          for( auto& h : list )
            if( h.first == name )
              return true;
      return false;
    }

    void install( lua_State* L );
    std::string definitions() const;
    std::vector< std::string > verify( lua_State* L ) const;

  private:
    std::set< std::string > m_installedGlobals;

    uint64_t maskFor( const ClassDecl& c ) const
    {
      uint64_t mask = 0;
      for( auto& other : classes )
      {
        for( const ClassDecl* walk = &other; walk; walk = findClass( walk->base ) )
        {
          if( walk->name == c.name )
          {
            if( other.tag >= 0 && other.tag < 64 )
              mask |= uint64_t( 1 ) << other.tag;
            break;
          }
        }
      }
      return mask;
    }

    const ClassDecl* findClass( const std::string& name ) const
    {
      if( name.empty() )
        return nullptr;
      for( auto& c : classes )
        if( c.name == name )
          return &c;
      return nullptr;
    }

    // a class method that __namecall calls without going through its closure
    struct NativeMethod
    {
      int ( *invoke )( lua_State*, void* functor, uint64_t mask, const char* className ) = nullptr;
      void* functor = nullptr;
      uint64_t mask = 0;
      const char* className = nullptr; // of the declaring class
    };

    struct ClassDispatch
    {
      std::vector< NativeMethod > byAtom; // indexed by method name atom; no invoke = not a native method
    };

    // one per class with a metatable, rebuilt by install; the metatables point into it and into `classes`, so
    // nothing may be declared after install
    std::deque< ClassDispatch > m_dispatch;

    static int indexFn( lua_State* L );
    static int newindexFn( lua_State* L );
    static int namecallFn( lua_State* L );
    static int namecallCont( lua_State* L, int status );
  };

  //////////////////////////////////////////////////////////////////////////////////////////////// builders

  template< typename Payload >
  class Class
  {
  public:
    using Storage = typename Target< Payload >::Storage;

    Class( Registry& reg, const char* luauName, int tag ) : m_reg( reg ), m_name( luauName )
    {
      auto& d = decl();
      d.tag = tag;
      d.dtor = []( lua_State*, void* p ) { static_cast< Storage* >( p )->~Storage(); };
    }

    Class& inherits( const char* baseLuauName )
    {
      decl().base = baseLuauName;
      return *this;
    }

    Class& equality( lua_CFunction eq )
    {
      decl().eq = eq;
      return *this;
    }

    Class& rawIndex( lua_CFunction index, lua_CFunction newindex, const char* declaration )
    {
      decl().rawIndex = index;
      decl().rawNewIndex = newindex;
      decl().rawIndexType = declaration ? declaration : "";
      return *this;
    }

    // G: ( Target& ) -> R
    template< typename G >
    Class& prop( const char* name, G getter )
    {
      MemberDecl m;
      m.kind = MemberKind::Prop;
      m.name = name;
      m.ret = detail::retType< typename FnTraits< G >::Ret >();
      m.push = makePusher( std::move( getter ), name );
      decl().members.push_back( std::move( m ) );
      return *this;
    }

    // S: ( Target&, V ) -> void
    template< typename G, typename S >
    Class& prop( const char* name, G getter, S setter )
    {
      prop( name, std::move( getter ) );
      auto& m = decl().members.back();
      m.writable = true;
      m.pushSetter = makePusher( std::move( setter ), name );
      return *this;
    }

    // F: ( Target&, Args... ) -> R
    template< typename F >
    Class& method( const char* name, F fn, const char* params = "" )
    {
      using T = FnTraits< F >;
      using Rest = typename detail::TupleTail< typename T::Args >::Tail;
      MemberDecl m;
      m.kind = MemberKind::Method;
      m.name = name;
      m.params = detail::joinParams( detail::ParamTypes< Rest >::get(), params, true );
      m.ret = detail::retType< typename T::Ret >();
      m.invoke = &detail::memberInvoke< Payload, F >;
      m.functor = std::make_shared< F >( fn );
      m.push = makePusher( std::move( fn ), name );
      decl().members.push_back( std::move( m ) );
      return *this;
    }

    // yielding or variadic; signature verbatim, e.g. "(self, eventId: number) -> boolean"
    Class& rawMethod( const char* name, lua_CFunction fn, const char* signature )
    {
      auto [ params, ret ] = detail::parseSignature( signature );
      MemberDecl m;
      m.kind = MemberKind::Raw;
      m.name = name;
      m.params = params;
      m.ret = ret;
      std::string debugName = m_name + "." + name;
      m.push = [ fn, debugName ]( lua_State* L, uint64_t ) { lua_pushcfunction( L, fn, debugName.c_str() ); };
      decl().members.push_back( std::move( m ) );
      return *this;
    }

    // implemented in common.luau; declaration only
    Class& luauMethod( const char* name, const char* signature )
    {
      auto [ params, ret ] = detail::parseSignature( signature );
      MemberDecl m;
      m.kind = MemberKind::Luau;
      m.name = name;
      m.params = params;
      m.ret = ret;
      decl().members.push_back( std::move( m ) );
      return *this;
    }

    // creates a handle userdata on the stack
    static void push( lua_State* L, int tag, Storage value )
    {
      void* mem = lua_newuserdatataggedwithmetatable( L, sizeof( Storage ), tag );
      new( mem ) Storage( std::move( value ) );
    }

  private:
    Registry& m_reg;
    std::string m_name;

    ClassDecl& decl() { return m_reg.classDecl( m_name ); }

    template< typename F >
    ClosurePusher makePusher( F fn, const char* name )
    {
      std::string cls = m_name;
      std::string debugName = m_name + "." + name;
      return [ fn = std::move( fn ), cls, debugName ]( lua_State* L, uint64_t mask )
      {
        detail::pushFunctor( L, fn );
        static_assert( sizeof( void* ) >= sizeof( uint64_t ), "tag mask stored as light userdata" );
        lua_pushlightuserdata( L, reinterpret_cast< void* >( static_cast< uintptr_t >( mask ) ) );
        lua_pushlstring( L, cls.data(), cls.size() );
        lua_pushcclosurek( L, &detail::memberThunk< Payload, F >, debugName.c_str(), 3, nullptr );
      };
    }
  };

  // A global table of functions ("log", "linkshell"); hostPrimitives = fields of __host, not declared.
  class Library
  {
  public:
    Library( Registry& reg, const char* luauName, bool hostPrimitives = false ) :
      m_reg( reg ), m_name( luauName ), m_host( hostPrimitives )
    {
      m_reg.libraryDecl( m_name, m_host );
    }

    template< typename F >
    Library& function( const char* name, F fn, const char* params = "" )
    {
      using T = FnTraits< F >;
      MemberDecl m;
      m.kind = MemberKind::Method;
      m.name = name;
      m.params = detail::joinParams( detail::ParamTypes< typename T::Args >::get(), params, false );
      m.ret = detail::retType< typename T::Ret >();
      std::string debugName = m_name + "." + name;
      m.push = [ fn = std::move( fn ), debugName ]( lua_State* L, uint64_t )
      {
        detail::pushFunctor( L, fn );
        lua_pushcclosurek( L, &detail::freeThunk< F >, debugName.c_str(), 1, nullptr );
      };
      decl().functions.push_back( std::move( m ) );
      return *this;
    }

    Library& rawFunction( const char* name, lua_CFunction fn, const char* signature )
    {
      auto [ params, ret ] = detail::parseSignature( signature );
      MemberDecl m;
      m.kind = MemberKind::Raw;
      m.name = name;
      m.params = params;
      m.ret = ret;
      std::string debugName = m_name + "." + name;
      m.push = [ fn, debugName ]( lua_State* L, uint64_t ) { lua_pushcfunction( L, fn, debugName.c_str() ); };
      decl().functions.push_back( std::move( m ) );
      return *this;
    }

    Library& luauFunction( const char* name, const char* signature )
    {
      auto [ params, ret ] = detail::parseSignature( signature );
      MemberDecl m;
      m.kind = MemberKind::Luau;
      m.name = name;
      m.params = params;
      m.ret = ret;
      decl().functions.push_back( std::move( m ) );
      return *this;
    }

    // a non-function member defined in common.luau (task.CANCELLED); declared as `name: luauType`
    Library& luauValue( const char* name, const char* luauType )
    {
      MemberDecl m;
      m.kind = MemberKind::Luau;
      m.name = name;
      m.ret = luauType;
      m.value = true;
      decl().functions.push_back( std::move( m ) );
      return *this;
    }

  private:
    Registry& m_reg;
    std::string m_name;
    bool m_host;

    LibraryDecl& decl() { return m_reg.libraryDecl( m_name, m_host ); }
  };

  class Enum
  {
  public:
    Enum( Registry& reg, const char* luauName ) : m_reg( reg ), m_index( reg.enums.size() )
    {
      reg.enums.push_back( EnumDecl{ luauName, {}, {} } );
    }

    Enum& value( const char* name, double v )
    {
      m_reg.enums[ m_index ].numbers.emplace_back( name, v );
      return *this;
    }

    Enum& value( const char* name, const char* v )
    {
      m_reg.enums[ m_index ].strings.emplace_back( name, v );
      return *this;
    }

  private:
    Registry& m_reg;
    size_t m_index;
  };

#define LUAU_ENUM_VALUE( builder, scope, name ) ( builder ).value( #name, static_cast< double >( scope::name ) )

  //////////////////////////////////////////////////////////////////////////////////////////////// Registry impl

  // upvalues: 1 getters, 2 method table chain (array), 3 raw index or nil, 4 class name
  inline int Registry::indexFn( lua_State* L )
  {
    if( lua_type( L, 2 ) == LUA_TSTRING )
    {
      lua_pushvalue( L, 2 );
      if( lua_rawget( L, lua_upvalueindex( 1 ) ) != LUA_TNIL )
      {
        lua_pushvalue( L, 1 );
        lua_call( L, 1, 1 );
        return 1;
      }
      lua_pop( L, 1 );

      int n = lua_objlen( L, lua_upvalueindex( 2 ) );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, lua_upvalueindex( 2 ), i );
        lua_pushvalue( L, 2 );
        if( lua_rawget( L, -2 ) != LUA_TNIL )
          return 1;
        lua_pop( L, 2 );
      }
    }

    if( !lua_isnil( L, lua_upvalueindex( 3 ) ) )
    {
      lua_pushvalue( L, lua_upvalueindex( 3 ) );
      lua_pushvalue( L, 1 );
      lua_pushvalue( L, 2 );
      lua_call( L, 2, 1 );
      return 1;
    }

    const char* key = lua_type( L, 2 ) == LUA_TSTRING ? lua_tostring( L, 2 ) : luaL_typename( L, 2 );
    luaL_error( L, "%s has no member '%s'", lua_tostring( L, lua_upvalueindex( 4 ) ), key );
    return 0;
  }

  // upvalues: 1 setters, 2 raw newindex or nil, 3 class name
  inline int Registry::newindexFn( lua_State* L )
  {
    if( lua_type( L, 2 ) == LUA_TSTRING )
    {
      lua_pushvalue( L, 2 );
      if( lua_rawget( L, lua_upvalueindex( 1 ) ) != LUA_TNIL )
      {
        lua_pushvalue( L, 1 );
        lua_pushvalue( L, 3 );
        lua_call( L, 2, 0 );
        return 0;
      }
      lua_pop( L, 1 );
    }

    if( !lua_isnil( L, lua_upvalueindex( 2 ) ) )
    {
      lua_pushvalue( L, lua_upvalueindex( 2 ) );
      lua_pushvalue( L, 1 );
      lua_pushvalue( L, 2 );
      lua_pushvalue( L, 3 );
      lua_call( L, 3, 0 );
      return 0;
    }

    const char* key = lua_type( L, 2 ) == LUA_TSTRING ? lua_tostring( L, 2 ) : luaL_typename( L, 2 );
    luaL_error( L, "%s.%s is read-only or unknown", lua_tostring( L, lua_upvalueindex( 3 ) ), key );
    return 0;
  }

  // upvalues: 1 ClassDispatch (light userdata), 2 method table chain (array), 3 class name
  inline int Registry::namecallFn( lua_State* L )
  {
    int atom = -1;
    const char* name = lua_namecallatom( L, &atom );
    auto* dispatch = static_cast< const ClassDispatch* >( lua_tolightuserdata( L, lua_upvalueindex( 1 ) ) );
    if( atom >= 0 && static_cast< size_t >( atom ) < dispatch->byAtom.size() )
    {
      const NativeMethod& m = dispatch->byAtom[ static_cast< size_t >( atom ) ];
      if( m.invoke )
        return m.invoke( L, m.functor, m.mask, m.className );
    }

    // raw methods and methods implemented in common.luau: either may yield, so the call goes through the continuation
    if( name )
    {
      int n = lua_objlen( L, lua_upvalueindex( 2 ) );
      for( int i = 1; i <= n; ++i )
      {
        lua_rawgeti( L, lua_upvalueindex( 2 ), i );
        lua_rawgetfield( L, -1, name );
        if( !lua_isnil( L, -1 ) )
        {
          lua_remove( L, -2 );
          lua_insert( L, 1 );
          return lua_callyieldable( L, lua_gettop( L ) - 1, LUA_MULTRET );
        }
        lua_pop( L, 2 );
      }
    }
    luaL_error( L, "%s has no method '%s'", lua_tostring( L, lua_upvalueindex( 3 ) ), name ? name : "?" );
    return 0;
  }

  // the method called by namecallFn has returned: its results replaced the whole frame
  inline int Registry::namecallCont( lua_State* L, int )
  {
    return lua_gettop( L );
  }

  inline void Registry::install( lua_State* L )
  {
    lua_callbacks( L )->useratom = &detail::userAtom;
    m_dispatch.clear();

    // method tables (global, shared with common.luau)
    for( auto& c : classes )
    {
      lua_getglobal( L, c.name.c_str() );
      if( !lua_istable( L, -1 ) )
      {
        lua_pop( L, 1 );
        lua_newtable( L );
        lua_pushvalue( L, -1 );
        lua_setglobal( L, c.name.c_str() );
      }
      uint64_t mask = maskFor( c );
      for( auto& m : c.members )
      {
        if( m.kind != MemberKind::Method && m.kind != MemberKind::Raw )
          continue;
        m.push( L, mask );
        lua_setfield( L, -2, m.name.c_str() );
      }
      lua_pop( L, 1 );
    }

    // metatables
    for( auto& c : classes )
    {
      if( c.tag < 0 )
        continue;

      std::vector< const ClassDecl* > chain; // most derived first
      for( const ClassDecl* walk = &c; walk; walk = findClass( walk->base ) )
        chain.push_back( walk );

      lua_newtable( L ); // metatable

      // flattened getters / setters, base first so derived ones override
      lua_newtable( L );
      lua_newtable( L );
      for( auto it = chain.rbegin(); it != chain.rend(); ++it )
      {
        uint64_t memberMask = maskFor( **it );
        for( auto& m : ( *it )->members )
        {
          if( m.kind != MemberKind::Prop )
            continue;
          m.push( L, memberMask );
          lua_setfield( L, -3, m.name.c_str() );
          if( m.writable )
          {
            m.pushSetter( L, memberMask );
            lua_setfield( L, -2, m.name.c_str() );
          }
        }
      }
      // stack: mt, getters, setters
      lua_newtable( L ); // method chain
      int i = 1;
      for( auto* walk : chain )
      {
        lua_getglobal( L, walk->name.c_str() );
        lua_rawseti( L, -2, i++ );
      }
      // stack: mt, getters, setters, chain
      lua_CFunction rawIndex = nullptr;
      lua_CFunction rawNewIndex = nullptr;
      for( auto* walk : chain )
      {
        if( !rawIndex && walk->rawIndex )
          rawIndex = walk->rawIndex;
        if( !rawNewIndex && walk->rawNewIndex )
          rawNewIndex = walk->rawNewIndex;
      }

      lua_pushvalue( L, -3 ); // getters
      lua_pushvalue( L, -2 ); // chain
      if( rawIndex )
        lua_pushcfunction( L, rawIndex, ( c.name + ".__rawindex" ).c_str() );
      else
        lua_pushnil( L );
      lua_pushstring( L, c.name.c_str() );
      lua_pushcclosurek( L, &Registry::indexFn, ( c.name + ".__index" ).c_str(), 4, nullptr );
      lua_setfield( L, -5, "__index" ); // stack: mt, getters, setters, chain, closure

      lua_pushvalue( L, -2 ); // setters
      if( rawNewIndex )
        lua_pushcfunction( L, rawNewIndex, ( c.name + ".__rawnewindex" ).c_str() );
      else
        lua_pushnil( L );
      lua_pushstring( L, c.name.c_str() );
      lua_pushcclosurek( L, &Registry::newindexFn, ( c.name + ".__newindex" ).c_str(), 3, nullptr );
      lua_setfield( L, -5, "__newindex" );

      // methods by name atom, base first so derived ones override. A raw or Luau method takes the slot of an
      // inherited native one away: it is found through the method tables instead
      auto& dispatch = m_dispatch.emplace_back();
      for( auto it = chain.rbegin(); it != chain.rend(); ++it )
      {
        uint64_t memberMask = maskFor( **it );
        for( auto& m : ( *it )->members )
        {
          if( m.kind == MemberKind::Prop )
            continue;
          int16_t atom = m.invoke ? detail::internAtom( m.name ) : detail::findAtom( m.name.data(), m.name.size() );
          if( atom < 0 )
            continue;
          if( dispatch.byAtom.size() <= static_cast< size_t >( atom ) )
            dispatch.byAtom.resize( static_cast< size_t >( atom ) + 1 );
          dispatch.byAtom[ static_cast< size_t >( atom ) ] =
            m.invoke ? NativeMethod{ m.invoke, m.functor.get(), memberMask, ( *it )->name.c_str() } : NativeMethod{};
        }
      }
      lua_pushlightuserdata( L, &dispatch );
      lua_pushvalue( L, -2 ); // chain
      lua_pushstring( L, c.name.c_str() );
      lua_pushcclosurek( L, &Registry::namecallFn, "__namecall", 3, &Registry::namecallCont );
      lua_setfield( L, -5, "__namecall" );

      lua_pop( L, 3 ); // getters, setters, chain

      lua_pushstring( L, c.name.c_str() );
      lua_setfield( L, -2, "__type" );
      lua_pushstring( L, c.name.c_str() );
      lua_setfield( L, -2, "__metatable" );

      lua_CFunction eq = nullptr;
      for( auto* walk : chain )
        if( !eq && walk->eq )
          eq = walk->eq;
      if( eq )
      {
        // one closure per function pointer, so handles of related classes compare with the same __eq
        lua_pushlightuserdata( L, reinterpret_cast< void* >( eq ) );
        lua_rawget( L, LUA_REGISTRYINDEX );
        if( lua_isnil( L, -1 ) )
        {
          lua_pop( L, 1 );
          lua_pushcfunction( L, eq, "__eq" );
          lua_pushlightuserdata( L, reinterpret_cast< void* >( eq ) );
          lua_pushvalue( L, -2 );
          lua_rawset( L, LUA_REGISTRYINDEX );
        }
        lua_setfield( L, -2, "__eq" );
      }

      lua_setreadonly( L, -1, true );
      lua_setuserdatametatable( L, c.tag );
      lua_setuserdatadtor( L, c.tag, c.dtor );
    }

    // libraries
    for( auto& lib : libraries )
    {
      bool hasNative = false;
      for( auto& f : lib.functions )
        hasNative |= f.kind != MemberKind::Luau;
      if( !hasNative )
        continue;

      const char* tableName = lib.host ? "__host" : lib.name.c_str();
      lua_getglobal( L, tableName );
      if( !lua_istable( L, -1 ) )
      {
        lua_pop( L, 1 );
        lua_newtable( L );
        lua_pushvalue( L, -1 );
        lua_setglobal( L, tableName );
      }
      for( auto& f : lib.functions )
      {
        if( f.kind == MemberKind::Luau )
          continue;
        f.push( L, 0 );
        lua_setfield( L, -2, f.name.c_str() );
      }
      lua_pop( L, 1 );
    }

    // enums
    for( auto& e : enums )
    {
      lua_newtable( L );
      for( auto& [ n, v ] : e.numbers )
      {
        lua_pushnumber( L, v );
        lua_setfield( L, -2, n.c_str() );
      }
      for( auto& [ n, v ] : e.strings )
      {
        lua_pushstring( L, v.c_str() );
        lua_setfield( L, -2, n.c_str() );
      }
      lua_setreadonly( L, -1, true );
      lua_setglobal( L, e.name.c_str() );
    }

    // singleton globals
    for( auto& g : globals )
    {
      if( !g.push )
        continue;
      g.push( L );
      lua_setglobal( L, g.name.c_str() );
    }

    m_installedGlobals.clear();
    lua_pushnil( L );
    while( lua_next( L, LUA_GLOBALSINDEX ) != 0 )
    {
      if( lua_type( L, -2 ) == LUA_TSTRING )
        m_installedGlobals.insert( lua_tostring( L, -2 ) );
      lua_pop( L, 1 );
    }
  }

  inline std::string Registry::definitions() const
  {
    std::string out;
    out += "-- Generated by the world server from Script/Luau/Bindings. Do not edit.\n";
    if( !headerComment.empty() )
      out += headerComment;
    for( auto& [ kind, list ] : handlerSets )
    {
      out += "--\n-- " + kind + " script handlers:\n";
      for( auto& [ name, sig ] : list )
      {
        auto [ params, ret ] = detail::parseSignature( sig );
        out += "--   function " + name + "(" + params + ")" + ( ret == "()" ? "" : ": " + ret ) + "\n";
      }
    }
    out += "\n";

    for( auto& [ name, type ] : aliases )
      out += "type " + name + " = " + type + "\n";
    if( !aliases.empty() )
      out += "\n";

    auto emitMembers = [ & ]( const std::vector< MemberDecl >& members )
    {
      for( auto& m : members )
      {
        if( m.kind == MemberKind::Prop )
          out += "  " + m.name + ": " + m.ret + "\n";
        else
          out += "  function " + m.name + "(" + m.params + "): " + m.ret + "\n";
      }
    };

    for( auto& c : classes )
    {
      // Luau 0.748 definition syntax ("declare class" is deprecated)
      out += "declare extern type " + c.name + ( c.base.empty() ? "" : " extends " + c.base ) + " with\n";
      emitMembers( c.members );
      if( !c.rawIndexType.empty() )
        out += "  " + c.rawIndexType + "\n";
      out += "end\n\n";
    }

    for( auto& [ nameAndBase, body ] : luauClasses )
    {
      out += "declare extern type " + nameAndBase + " with\n";
      out += body;
      if( !body.empty() && body.back() != '\n' )
        out += "\n";
      out += "end\n\n";
    }

    for( auto& lib : libraries )
    {
      if( lib.host )
        continue;
      out += "declare " + lib.name + ": {\n";
      for( auto& f : lib.functions )
        out += f.value ? "  " + f.name + ": " + f.ret + ",\n" : "  " + f.name + ": (" + f.params + ") -> " + f.ret + ",\n";
      out += "}\n\n";
    }

    for( auto& e : enums )
    {
      out += "declare " + e.name + ": {\n";
      for( auto& [ n, v ] : e.numbers )
        out += "  " + n + ": number,\n";
      for( auto& [ n, v ] : e.strings )
        out += "  " + n + ": string,\n";
      out += "}\n\n";
    }

    for( auto& g : globals )
      if( !g.type.empty() )
        out += "declare " + g.name + ": " + g.type + "\n";
    if( !globals.empty() )
      out += "\n";

    for( auto& [ name, sig ] : globalFunctions )
    {
      auto [ params, ret ] = detail::parseSignature( sig );
      out += "declare function " + name + "(" + params + "): " + ret + "\n";
    }
    return out;
  }

  inline std::vector< std::string > Registry::verify( lua_State* L ) const
  {
    std::vector< std::string > problems;
    const std::string hint = " has no type declaration; add it to Bindings/BindCommon.cpp";

    auto declaredIn = [ & ]( const std::vector< MemberDecl >& members, const std::string& name )
    {
      for( auto& m : members )
        if( m.name == name && m.kind != MemberKind::Prop )
          return true;
      return false;
    };

    auto checkTable = [ & ]( const std::string& table, const std::vector< MemberDecl >& members )
    {
      lua_getglobal( L, table.c_str() );
      if( !lua_istable( L, -1 ) )
      {
        bool anyLuau = false;
        for( auto& m : members )
          anyLuau |= m.kind == MemberKind::Luau;
        if( anyLuau )
          for( auto& m : members )
            if( m.kind == MemberKind::Luau )
              problems.push_back( table + "." + m.name + " is declared in the bindings but common.luau does not define it" );
        lua_pop( L, 1 );
        return;
      }
      for( auto& m : members )
      {
        if( m.kind != MemberKind::Luau )
          continue;
        lua_rawgetfield( L, -1, m.name.c_str() );
        if( m.value ? lua_isnil( L, -1 ) : !lua_isfunction( L, -1 ) )
          problems.push_back( table + "." + m.name + " is declared in the bindings but common.luau does not define it" );
        lua_pop( L, 1 );
      }
      std::set< std::string > undeclared;
      lua_pushnil( L );
      while( lua_next( L, -2 ) != 0 )
      {
        if( lua_type( L, -2 ) == LUA_TSTRING && lua_isfunction( L, -1 ) )
        {
          std::string key = lua_tostring( L, -2 );
          if( !declaredIn( members, key ) )
            undeclared.insert( key );
        }
        lua_pop( L, 1 );
      }
      for( auto& key : undeclared )
        problems.push_back( table + "." + key + hint );
      lua_pop( L, 1 );
    };

    for( auto& c : classes )
      checkTable( c.name, c.members );
    for( auto& lib : libraries )
      if( !lib.host )
        checkTable( lib.name, lib.functions );

    for( auto& [ name, sig ] : globalFunctions )
    {
      lua_getglobal( L, name.c_str() );
      if( !lua_isfunction( L, -1 ) )
        problems.push_back( name + " is declared in the bindings but common.luau does not define it" );
      lua_pop( L, 1 );
    }

    // globals created by common.luau
    std::set< std::string > created;
    lua_pushnil( L );
    while( lua_next( L, LUA_GLOBALSINDEX ) != 0 )
    {
      if( lua_type( L, -2 ) == LUA_TSTRING )
      {
        std::string key = lua_tostring( L, -2 );
        if( !m_installedGlobals.count( key ) && key != "__host" )
          created.insert( key );
      }
      lua_pop( L, 1 );
    }
    for( auto& key : created )
    {
      bool known = false;
      for( auto& lib : libraries )
        known |= !lib.host && lib.name == key;
      for( auto& c : classes )
        known |= c.name == key;
      for( auto& [ n, s ] : globalFunctions )
        known |= n == key;
      for( auto& g : globals )
        known |= g.name == key;
      if( !known )
        problems.push_back( key + hint );
    }
    return problems;
  }
}
