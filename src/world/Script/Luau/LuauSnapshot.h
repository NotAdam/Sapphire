#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <Common.h>

namespace Sapphire::Scripting
{
  class LuauRuntime;

  struct SavedValue;
  using SavedTable = std::vector< std::pair< SavedValue, SavedValue > >;

  // plain data copied out of the VM (director.data across a VM restart)
  struct SavedValue
  {
    std::variant< std::monostate, bool, double, std::string, Common::Vector3, std::shared_ptr< SavedTable > > v;
  };

  // director.data tables: plain Luau tables referenced from the registry of the main state
  namespace LuauSnapshot
  {
    // lua_ref of a fresh empty table
    int newTable( LuauRuntime& runtime );
    // lua_unref; no-op for -1
    void release( LuauRuntime& runtime, int ref );
    // in place: remove function/thread values and keys and metatables (they would pin the code of a closed context);
    // readonly tables cannot be changed, their code is reported in dropped instead
    void stripCode( LuauRuntime& runtime, int ref, std::vector< std::string >& dropped );
    // copy plain data out of the VM
    SavedValue capture( LuauRuntime& runtime, int ref, std::vector< std::string >& dropped );
    // lua_ref of a rebuilt table
    int restore( LuauRuntime& runtime, const SavedValue& value );
    // "key = value" lines, sorted by key, 2 spaces per depth
    std::vector< std::string > print( LuauRuntime& runtime, int ref );
  }
}
