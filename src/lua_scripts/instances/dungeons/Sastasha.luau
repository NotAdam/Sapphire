script.instance(4)

local E   = require("./data/SastashaEObjs")        -- E.memo[coralId] and E.static.<Name> are EObjRefs
local Enc = require("./data/SastashaEncounters")   -- Enc.Chopper / Madison / Madison2 / Denn are EncounterRefs
local S   = E.static

local Coral  = { Blue = 2000213, Red = 2000214, Green = 2000215 }
local Var    = { Seq = 0 }
local Seq    = { Seq1 = 1, Seq2 = 3, Seq3 = 7, Seq4 = 23, Seq5 = 31, Finish = 255 }
local Action = { Touch = 24, Short = 15 }
local Corals = { S.Bluecoralformation, S.Redcoralformation, S.Greencoralformation }

-- once per instance, never on reload
function onCreate()
  local coral = rng.range(Coral.Blue, Coral.Green)
  director.data.coral = coral
  E.memo[coral]:spawn()
  for _, ref in S do ref:spawn() end
end

-- on create and after every reload
function onStart()
  task.spawn(progression)
end

-- Var.Seq only ever increases, so each wait returns at once when already passed (reload catch-up)
function progression()
  wait.varAtLeast(Var.Seq, Seq.Seq2)
  Enc.Madison:start()
  wait.varAtLeast(Var.Seq, Seq.Seq5)
  Enc.Madison2:start()
  Enc.Madison2:wait(EncounterStatus.Success)
  Enc.Denn:start()
  wait.varAtLeast(Var.Seq, Seq.Finish)
  instance:dutyComplete()
end

function onLeaveTerritory(player)
  if director.vars[Var.Seq] ~= Seq.Finish then return end
  local q = player:quest(66211)
  if not q.active then q = player:quest(65781) end
  if q.active and q.seq == 3 then q.seq = 255 end
end

local function confirm(player, eventId)
  local res = player:playScene(eventId, 1, SceneFlags.HIDE_HOTBAR, { 1 })
  return (res.results[1] or 0) == 0
end

-- the three memo variants share one eobj name, so one registration covers them
on.talk(E.memo[Coral.Blue], function(player, eobj, eventId)
  player:playScene(eventId, 1, SceneFlags.HIDE_HOTBAR, { 1 })
end)

on.talk(Corals, function(player, eobj, eventId)
  if not confirm(player, eventId) then return end
  if not player:eventAction(director.id, Action.Touch) then return end
  eobj:hide()
  if eobj.baseId ~= director.data.coral then return end   -- TODO carried over from C++: spawn adds
  S.Inconspicuousswitch:show()
  director.vars[Var.Seq] = Seq.Seq1
  instance:logMessage(player, 2034, { 0, 0 })
  for _, coral in Corals do coral:hide() end
end)

on.talk(S.Inconspicuousswitch, function(player, eobj, eventId)
  local status = Enc.Chopper:status()
  if status == EncounterStatus.None then
    Enc.Chopper:start()
  elseif status == EncounterStatus.Success and Enc.Chopper:isCurrent() then
    if not player:eventAction(director.id, Action.Touch) then return end
    eobj:hide()
    S.Hiddendoor:setInvisibility(7)
    S.Hiddendoor:setCollision(false)
    director.vars[Var.Seq] = Seq.Seq2
    instance:logMessage(player, 2064, { 0, 0 })
  end
end)

local function key(seq, itemId)
  return function(player, eobj, eventId)
    if not player:eventAction(director.id, Action.Short) then return end
    eobj:hide()
    director.vars[Var.Seq] = seq
    instance:logMessage(player, 2031, { itemId })
  end
end
on.talk(S.Captainsquarterskey, key(Seq.Seq4, 2000512))
on.talk(S.WaveriderGatekey, key(Seq.Seq5, 2000513))

local function lockedDoor(requiredSeq, placeName)
  return function(player, eobj, eventId)
    if director.vars[Var.Seq] ~= requiredSeq then return end
    if not confirm(player, eventId) then return end
    if not player:eventAction(director.id, Action.Touch) then return end
    eobj:hide()
    instance:logMessage(player, 2059, { placeName })
  end
end
on.talk(S.Captainsquarters, lockedDoor(Seq.Seq4, 668))
on.talk(S.WaveriderGate, lockedDoor(Seq.Seq5, 671))
