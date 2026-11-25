#pragma once

#include "../LuauBind.h"
#include "../LuauScheduler.h"

namespace Sapphire::Scripting
{
  class LuauContext;
  class LuauScriptMgr;
}

namespace Sapphire::Scripting::Bindings
{
  // userdata tags of the handle types
  namespace HandleTag
  {
    constexpr int Player = 1;
    constexpr int EObj = 2;
    constexpr int BNpc = 3;
    constexpr int Quest = 4;
    constexpr int Chara = 5;
    constexpr int Action = 6;
    constexpr int ActionResults = 7;
    constexpr int StatusEffect = 8;
    constexpr int ScopeDirector = 20;
    constexpr int ScopeDirectorVars = 21;
    constexpr int ScopeTerritory = 22;
    constexpr int ScopeInstance = 23;
    constexpr int ScopeBattle = 24;
    constexpr int Counter = 40; // selftest
  }

  // core: host primitives (__host), log, rng, require, script, on. No world services needed.
  void bindCore( Bind::Registry& reg );
  // declarations for everything common.luau implements, script handler lists, header comment
  void bindCommon( Bind::Registry& reg );

  // world bindings (need the world services and LuauScriptMgr)
  void bindEnums( Bind::Registry& reg );
  void bindPlayer( Bind::Registry& reg );
  void bindQuest( Bind::Registry& reg );
  void bindActors( Bind::Registry& reg );
  void bindScope( Bind::Registry& reg );
  void bindWorld( Bind::Registry& reg );
  void bindAction( Bind::Registry& reg );
  void bindStatus( Bind::Registry& reg );

  // the task that is running: L is its thread or a coroutine resumed inside it. nullptr outside script code
  ScriptThread* callingThread( lua_State* L );
  // the calling thread's context; raises a Luau error when there is none
  LuauContext& callingContext( lua_State* L );
  LuauScheduler& scheduler( lua_State* L );
  // checks that a yielding host function may yield here; returns the task thread
  ScriptThread& beginYield( lua_State* L );
}
