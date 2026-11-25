// Declarations for everything src/lua_scripts/common.luau implements on top of the host primitives.
// Every global function or table member that common.luau defines must be declared here, otherwise the
// server reports it at startup (Registry::verify).

#include "Bindings.h"

namespace Sapphire::Scripting::Bindings
{
  void bindCommon( Bind::Registry& reg )
  {
    reg.headerComment =
      "--\n"
      "-- Writing scripts that survive a reload (saving a .luau file reloads it into every running instance):\n"
      "--   * a reload cancels every thread of the script; locals are gone, onStart runs again (but not onCreate)\n"
      "--   * anything a long-running thread must remember goes in director.data or a director var before the next wait\n"
      "--   * side effects between two waits must be idempotent host calls (EObjRef:spawn, EncounterRef:start,\n"
      "--     instance:dutyComplete) or wrapped in once( key, fn )\n"
      "--   * plain task.wait restarts its full duration after a reload; use wait.timer( key, seconds ) in such threads\n"
      "--   * flag parameters take a number or an array of numbers: { SceneFlags.HIDE_HOTBAR, SceneFlags.NO_DEFAULT_CAMERA }\n"
      "--\n"
      "-- Tasks and waits:\n"
      "--   * task.spawn / task.delay cannot run in top-level script code; start tasks from onStart, onCreate or a handler\n"
      "--   * wait.till( pred ) does not poll: pred is re-checked only when the scope reports a change (director vars,\n"
      "--     seq, flags, custom vars, instance state, encounter status, actor death) or after another step of the same\n"
      "--     script ran (so director.data and globals the script writes itself are seen)\n"
      "--   * state without change notifications (actor hp / position, enemy counts, player state) needs\n"
      "--     wait.poll( pred, interval? ), which also re-checks every interval seconds (default 0.1)\n"
      "--   * Task:await() returns false, task.CANCELLED for a cancelled task (compare with ==)\n"
      "--   * task.onExit returns an ExitHook; hook:remove() unregisters the callback\n"
      "--\n"
      "-- Handlers:\n"
      "--   * a handler is the global function of that name once the script has loaded (its top-level code and requires);\n"
      "--     assigning a handler global later, from inside a function, has no effect until the script is reloaded\n";

    reg.typeAlias( "CollisionBox", "{ pos: vector, rotation: number, width: number, height: number, depth: number }" );
    reg.typeAlias( "EObjDef",
                   "{ name: string, baseId: number, boundId: number, instanceId: number, state: number, pos: vector, "
                   "scale: number, rotation: number, invisibility: number?, collision: {CollisionBox}?, "
                   "collisionEnabled: boolean? }" );
    // enumerated strings are typed `string` (with the values in a comment): table literals in data modules infer
    // `string` for them, which a singleton union would reject in strict mode
    reg.typeAlias( "EncounterEObjDef",
                   "{ name: string, baseId: number, boundId: number, instanceId: number, state: number, pos: vector, "
                   "scale: number, rotation: number, invisibility: number?, remove: string? --[[ none | fail | success ]] }" );
    reg.typeAlias( "EncounterBNpcDef",
                   "{ layoutId: number, hp: number, type: string? --[[ enemy | friendly ]], flags: (number | {number})?, "
                   "boss: boolean? }" );
    reg.typeAlias( "EncounterDef",
                   "{ name: string, timeline: string, shape: string? --[[ box | cylinder | polygon ]], polygon: string?, "
                   "position: vector?, position2: vector?, bnpcs: {EncounterBNpcDef}?, initEObjs: {EncounterEObjDef}?, "
                   "successEObjs: {EncounterEObjDef}?, lockoutEntrances: {EncounterEObjDef}?, "
                   "lockoutExits: {EncounterEObjDef}?, lockout: boolean?, placeName: number?, bgmCombat: number?, "
                   "bgmRestore: number?, bgmFinish: number? }" );
    reg.typeAlias( "SceneResult",
                   "{ kind: \"return\", errorCode: number, results: {number}, string: string, int: number }" );
    reg.typeAlias( "SceneYield", "{ kind: \"yield\", yieldId: number, string: string, int: number }" );
    reg.typeAlias( "SceneYieldHandler",
                   "((y: SceneYield) -> {number}?) | { [number]: (y: SceneYield) -> {number}? }" );
    reg.typeAlias( "NestOptions", "{ param1: number?, param2: number? }" );

    reg.luauClass( "Task",
                   "  id: number\n"
                   "  done: boolean\n"
                   "  function await(self): (boolean, ...any)\n"
                   "  function cancel(self): ()\n" );
    reg.luauClass( "TaskCancelled", "" );
    reg.luauClass( "ExitHook", "  function remove(self): ()\n" );
    reg.luauClass( "EObjRef extends EObj",
                   "  def: EObjDef\n"
                   "  function spawn(self): EObj\n"
                   "  function exists(self): boolean\n"
                   "  function get(self): EObj?\n" );
    reg.luauClass( "EncounterRef",
                   "  def: EncounterDef\n"
                   "  name: string\n"
                   "  function start(self): ()\n"
                   "  function status(self): string\n"
                   "  function isCurrent(self): boolean\n"
                   "  function wait(self, status: string): ()\n" );
    reg.luauClass( "ChildEvent",
                   "  id: number\n"
                   "  player: Player\n"
                   "  function playScene(self, scene: number, flags: (number | {number})?, params: {number}?, "
                   "onYield: SceneYieldHandler?): SceneResult\n" );

    Bind::Library( reg, "task" )
      .luauValue( "CANCELLED", "TaskCancelled" )
      .luauFunction( "wait", "(seconds: number?) -> ()" )
      .luauFunction( "onExit", "(fn: (reason: string) -> ()) -> ExitHook" )
      .luauFunction( "clock", "() -> number" )
      .luauFunction( "spawn", "(fn: (...any) -> ...any, ...any) -> Task" )
      .luauFunction( "delay", "(seconds: number, fn: (...any) -> ...any, ...any) -> Task" );

    Bind::Library( reg, "async" )
      .luauFunction( "all", "(fns: { [any]: () -> any }) -> { [any]: any }" )
      .luauFunction( "race", "(fns: { [any]: () -> any }) -> (any, any)" )
      .luauFunction( "timeout", "(seconds: number, fn: () -> any) -> (boolean, any)" );

    Bind::Library( reg, "wait" )
      .luauFunction( "till", "(pred: () -> boolean) -> ()" ) // "until" is a reserved word in Luau
      .luauFunction( "poll", "(pred: () -> boolean, interval: number?) -> ()" )
      .luauFunction( "var", "(index: number, value: number) -> ()" )
      .luauFunction( "varAtLeast", "(index: number, value: number) -> ()" )
      .luauFunction( "seq", "(value: number) -> ()" )
      .luauFunction( "state", "(state: number) -> ()" )
      .luauFunction( "timer", "(key: string, seconds: number) -> ()" );

    reg.globalFunction( "once", "(key: string, fn: () -> ()) -> ()" );
    reg.globalFunction( "eobjDef", "(def: EObjDef) -> EObjRef" );
    reg.globalFunction( "encounterDef", "(def: EncounterDef) -> EncounterRef" );

    reg.handlers( "instance", {
      { "onCreate", "() -> ()" },
      { "onStart", "() -> ()" },
      { "onReset", "() -> ()" },
      { "onStateChange", "(old: number, new: number) -> ()" },
      { "onEnterTerritory", "(player: Player, eventId: number, param1: number, param2: number) -> ()" },
      { "onLeaveTerritory", "(player: Player) -> ()" },
      { "onDirectorVarChange", "(index: number, value: number) -> ()" },
      { "onDirectorSeqChange", "(seq: number) -> ()" },
      { "onDirectorFlagChange", "(flags: number) -> ()" },
      { "onActorDeath", "(actor: Player | BNpc) -> ()" },
      { "onTalk", "(player: Player, eobj: EObj?, eventId: number, actorId: number) -> ()" },
    } );
    reg.handlers( "quest", {
      { "onTalk", "(quest: Quest, player: Player, actorId: number) -> ()" },
      { "onSay", "(quest: Quest, player: Player, actorId: number, sayId: number) -> ()" },
      { "onEventItem", "(quest: Quest, player: Player, actorId: number) -> ()" },
      { "onEventGroundItem", "(quest: Quest, player: Player, pos: vector) -> ()" },
      { "onBNpcKill", "(quest: Quest, player: Player, bnpc: BNpc) -> ()" },
      { "onTriggerOwnerDeaggro", "(quest: Quest, player: Player, bnpc: BNpc) -> ()" },
      { "onEmote", "(quest: Quest, player: Player, actorId: number, emoteId: number) -> ()" },
      { "onEnterTerritory", "(quest: Quest, player: Player, param1: number, param2: number) -> ()" },
      { "onWithinRange", "(quest: Quest, player: Player, eventId: number, param1: number, pos: vector) -> ()" },
      { "onOutsideRange", "(quest: Quest, player: Player, eventId: number, param1: number, pos: vector) -> ()" },
      { "onPlayerDeath", "(quest: Quest, player: Player) -> ()" },
      { "onEObjHit", "(quest: Quest, player: Player, actorId: number, actionId: number) -> ()" },
      { "getQuestAvailability", "(player: Player, eventId: number) -> number" },
    } );
    reg.handlers( "event", {
      { "onTalk", "(player: Player, eventId: number, actorId: number) -> ()" },
      { "onEmote", "(player: Player, eventId: number, actorId: number, emoteId: number) -> ()" },
      { "onEnterTerritory", "(player: Player, eventId: number, param1: number, param2: number) -> ()" },
      { "onWithinRange", "(player: Player, eventId: number, param1: number, pos: vector) -> ()" },
      { "onOutsideRange", "(player: Player, eventId: number, param1: number, pos: vector) -> ()" },
      { "onTradeReturn", "(player: Player, eventId: number, subEvent: number, param: number, catalogId: number) -> ()" },
    } );
    reg.handlers( "questbattle", {
      { "onCreate", "() -> ()" },
      { "onStart", "() -> ()" },
      { "onPlayerSetup", "(player: Player) -> ()" },
      { "onDutyCommence", "(player: Player) -> ()" },
      { "onEnterTerritory", "(player: Player, eventId: number, param1: number, param2: number) -> ()" },
      { "onDutyComplete", "(player: Player) -> ()" },
    } );
    reg.handlers( "action", {
      { "onBeforeBootstrap", "(action: Action) -> ()" },
      { "onStart", "(action: Action) -> ()" },
      { "onExecute", "(action: Action) -> ()" },
      { "onInterrupt", "(action: Action) -> ()" },
      { "onAfterBuildEffect", "(action: Action) -> ()" },
    } );
    reg.handlers( "status", {
      { "onApply", "(actor: Chara) -> ()" },
      { "onTick", "(actor: Chara, effect: StatusEffect) -> ()" },
      { "onExpire", "(actor: Chara) -> ()" },
      { "onPlayerHit", "(actor: Chara, effect: StatusEffect) -> ()" },
    } );
    reg.handlers( "zone", {
      { "onZoneInit", "(territoryTypeId: number) -> ()" },
      { "onWithinRange", "(player: Player, eventId: number, param1: number, pos: vector) -> ()" },
    } );
    reg.handlers( "eobj", {
      { "onTalk", "(player: Player, eobj: EObj, eventId: number) -> ()" },
    } );
  }
}
