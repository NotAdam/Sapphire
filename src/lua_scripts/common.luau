-- Loaded once into the shared globals, before luaL_sandbox. Must not keep per-context state in upvalues.

-- Host primitives: __host is removed from the globals right after this file has run.
local host = __host
local __spawn, __current, __park, __wake, __cancel = host.spawn, host.current, host.park, host.wake, host.cancel
local __wait, __wait_change, __clock, __on_exit = host.wait, host.wait_change, host.clock, host.on_exit
local __off_exit = host.off_exit
local __scene_begin, __scene_await, __scene_resume = host.scene_begin, host.scene_await, host.scene_resume
local __event_start, __event_finish = host.event_start, host.event_finish

---------------------------------------------------------------- typed references to defined objects
-- eobjDef{...} and encounterDef{...} are used in data modules. A reference holds only the definition, so it is safe as a
-- file-level local and across reloads; the live object is looked up in the calling script's scope on every use.
local EObjRef = {}

local function liveEObj(ref)
  local h = territory:eobj(ref.name)
  if h == nil then error("eobj '" .. ref.name .. "' is not spawned", 3) end
  return h
end

function EObjRef.spawn(ref) return territory:spawnEObj(ref.def) end      -- returns the existing eobj when already spawned
function EObjRef.exists(ref) return territory:eobj(ref.name) ~= nil end
function EObjRef.get(ref) return territory:eobj(ref.name) end            -- EObj handle or nil

-- One forwarder per EObj method name, shared by every reference and context (it holds no scope: the live eobj is
-- looked up on each call). ref:show() -> live eobj:show()
local eobjMethods = EObj
local forwarders = {}

local eobjRefMeta = {
  __index = function(ref, key)
    local own = EObjRef[key]
    if own then return own end
    local method = eobjMethods[key]
    if method == nil then return liveEObj(ref)[key] end                    -- a property of the live eobj
    local forward = forwarders[key]
    if forward == nil then
      forward = function(self, ...) return method(liveEObj(self), ...) end
      forwarders[key] = forward
    end
    return forward
  end,
  __newindex = function(ref, key, value) liveEObj(ref)[key] = value end,
}

function eobjDef(def)
  return setmetatable({ def = def, name = def.name }, eobjRefMeta)
end

local EncounterRef = {}
EncounterRef.__index = EncounterRef
function EncounterRef:start() encounter.start(self.def) end              -- no-op when already started in this scope
function EncounterRef:status() return encounter.status(self.name) end
function EncounterRef:isCurrent() return encounter.current() == self.name end
function EncounterRef:wait(status)
  wait.till(function() return encounter.status(self.name) == status end)
end

function encounterDef(def)
  return setmetatable({ def = def, name = def.name }, EncounterRef)
end

---------------------------------------------------------------- task
local Task = {}
Task.__index = Task

-- the error value of Task:await() for a cancelled task; compare with ==
local CANCELLED = table.freeze(setmetatable({}, table.freeze({
  __tostring = function() return "task.CANCELLED" end,
})))

local function settle(t, ok, packed)
  t.done, t.ok, t.result = true, ok, packed
  local waiters = t.waiters
  t.waiters = {}
  for _, id in waiters do __wake(id) end
end

local function traceback(e) return debug.traceback(tostring(e), 2) end

-- handle returned by task.onExit
local ExitHook = {}
ExitHook.__index = ExitHook

-- unregisters the callback; no-op once it has run or was removed. Works from any thread, also while the registering
-- thread is running its exit callbacks.
function ExitHook:remove()
  if self.serial == nil then return end
  __off_exit(self.serial, self.thread)
  self.serial = nil
end

task = {}
task.CANCELLED = CANCELLED

function task.wait(seconds) __wait(seconds or 0) end
function task.onExit(fn) return setmetatable({ serial = __on_exit(fn), thread = __current() }, ExitHook) end
function task.clock() return __clock() end

-- label (task.delay) prefixes fn's error message
local function spawnTask(label, fn, ...)
  local t = setmetatable({ done = false, ok = false, result = nil, waiters = {} }, Task)
  local handler = if label then function(e) return debug.traceback(label .. tostring(e), 2) end else traceback
  t.id = __spawn(function(...)
    __on_exit(function(reason)
      if not t.done then settle(t, false, table.pack(false, if reason == "cancelled" then CANCELLED else reason)) end
    end)
    local r = table.pack(xpcall(fn, handler, ...))
    if not t.done then settle(t, r[1], r) end
    if not r[1] then error(r[2], 0) end
  end, ...)
  return t
end

-- Starts after the current thread next yields or returns.
function task.spawn(fn, ...) return spawnTask(nil, fn, ...) end

function task.delay(seconds, fn, ...)
  local where = "?"
  if type(fn) == "function" then
    local src, line, name = debug.info(fn, "sln")
    where = string.format("%s (%s:%d)", if name and name ~= "" then name else "<anonymous>", src, line)
  end
  local label = string.format("task.delay(%s) callback %s: ", tostring(seconds), where)
  local args = table.pack(...)
  return spawnTask(label, function()
    __wait(seconds)
    return fn(table.unpack(args, 1, args.n))
  end)
end

-- returns true, ...results | false, err (err == task.CANCELLED when the task was cancelled)
function Task:await()
  while not self.done do
    table.insert(self.waiters, __current())
    __park()
  end
  return table.unpack(self.result, 1, self.result.n)
end

function Task:cancel()
  if self.done then return end
  __cancel(self.id)
  -- a task cancelled before it first ran never registered its exit handler
  if not self.done then settle(self, false, table.pack(false, CANCELLED)) end
end

---------------------------------------------------------------- async
async = {}

-- waits for every function; returns { [key] = first result }; first error cancels the rest and is rethrown
-- (a subtask cancelled from elsewhere raises "async.all: a task was cancelled")
function async.all(fns)
  local tasks, out = {}, {}
  for k, fn in fns do tasks[k] = task.spawn(fn) end
  -- cancelling the waiting thread must not leak the subtasks
  local hook = __on_exit(function() for _, t in tasks do t:cancel() end end)
  for k, t in tasks do
    local r = table.pack(t:await())
    if not r[1] then
      __off_exit(hook)
      for _, other in tasks do other:cancel() end
      error(if r[2] == CANCELLED then "async.all: a task was cancelled" else r[2], 0)
    end
    out[k] = r[2]
  end
  __off_exit(hook)
  return out
end

-- returns key, firstResult of the first function to finish; cancels the rest; errors only if all fail
function async.race(fns)
  local me = __current()
  local winner, lastErr, pending = nil, nil, 0
  local tasks = {}
  for k, fn in fns do
    pending += 1
    tasks[k] = task.spawn(function()
      local ok, v = xpcall(fn, traceback)
      pending -= 1
      if winner ~= nil then return end
      if ok then
        winner = { key = k, value = v }
        __wake(me)
      elseif pending == 0 then
        lastErr = v
        __wake(me)
      end
    end)
  end
  if pending == 0 then error("async.race: no functions to race", 2) end
  -- cancelling the waiting thread (also async.timeout's) must not leak the subtasks
  local hook = __on_exit(function() for _, t in tasks do t:cancel() end end)
  while winner == nil and lastErr == nil do __park() end
  __off_exit(hook)
  for _, t in tasks do t:cancel() end
  if winner == nil then error(lastErr, 0) end
  return winner.key, winner.value
end

-- returns true, result | false when the time ran out
function async.timeout(seconds, fn)
  local key, value = async.race({ result = fn, timeout = function() __wait(seconds) end })
  if key == "timeout" then return false end
  return true, value
end

---------------------------------------------------------------- wait
wait = {}

-- Parks until pred() is true, re-checking it only when something it may depend on changed: a change the scope reports
-- (director vars / seq / flags / custom vars, instance state, encounter status, actor death) or the end of any other
-- step of this script's tasks and handlers (so director.data and script globals written by the script itself are seen).
-- interval: also re-check every interval seconds (wait.poll). pred must not yield.
local function waitFor(pred, interval)
  if pred() then return end
  -- the step that leads into the wait may have changed state: its end notifies the other waiters
  __wait_change(interval, false)
  -- later steps only re-evaluate pred, so they notify nobody (no waiter ping-pong)
  while not pred() do __wait_change(interval, true) end
end

-- Event-driven ("until" is a reserved word): no polling. For predicates on state without change notifications
-- (actor hp / position, enemy counts, player state) use wait.poll.
function wait.till(pred) waitFor(pred, nil) end
-- Opt-in polling: like wait.till, plus a re-check every interval seconds (default 0.1).
function wait.poll(pred, interval) waitFor(pred, interval or 0.1) end

function wait.var(index, value) wait.till(function() return director.vars[index] == value end) end
function wait.varAtLeast(index, value) wait.till(function() return director.vars[index] >= value end) end
function wait.seq(value) wait.till(function() return director.seq >= value end) end
function wait.state(state) wait.till(function() return instance.state == state end) end

-- A wait whose deadline survives reloads: the absolute deadline is stored in director.data the first time the key is seen.
-- Returns at once when the deadline has already passed. Use instead of task.wait in long-running instance threads.
function wait.timer(key, seconds)
  if director.data.__timers == nil then director.data.__timers = {} end
  local timers = director.data.__timers
  if timers[key] == nil then timers[key] = __clock() + seconds * 1000 end
  local remaining = (timers[key] - __clock()) / 1000
  if remaining > 0 then __wait(remaining) end
end

-- Runs fn at most once per instance lifetime, across reloads.
function once(key, fn)
  if director.data.__once == nil then director.data.__once = {} end
  local done = director.data.__once
  if done[key] then return end
  done[key] = true
  fn()
end

---------------------------------------------------------------- scenes and child events
-- onYield: nil | function(y) -> { values }? | { [yieldId] = function(y) -> { values }? }
-- y = { kind = "yield", yieldId, string, int }. A nil return (or no handler) resumes with { 0 }.
-- returns { kind = "return", errorCode, results = { ... }, string, int }
function Player.playScene(player, eventId, scene, flags, params, onYield)
  local s = __scene_begin(player, eventId, scene, flags or 0, params or {})
  while true do
    local ev = __scene_await(s)
    if ev.kind == "return" then return ev end
    local handler = if type(onYield) == "table" then onYield[ev.yieldId] else onYield
    local values = if handler then handler(ev) else nil
    __scene_resume(s, ev.yieldId, values or { 0 })
  end
end

local ChildEvent = {}
ChildEvent.__index = ChildEvent
function ChildEvent:playScene(scene, flags, params, onYield)
  return self.player:playScene(self.id, scene, flags, params, onYield)
end

-- Opens eventId as a child (Nest) event, runs body(child), always finishes the child, returns body's results.
-- opts = { param1 = number?, param2 = number? }
function Player.nest(player, actorId, eventId, body, opts)
  __event_start(player, actorId, eventId, opts and opts.param1 or 0, opts and opts.param2 or 0)
  local r = table.pack(pcall(body, setmetatable({ id = eventId, player = player }, ChildEvent)))
  __event_finish(player, eventId)
  if not r[1] then error(r[2], 0) end
  return table.unpack(r, 2, r.n)
end
