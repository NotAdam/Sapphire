script.questBattle(11)

local EObjs = require("./data/ChasingShadowsEObjs")

local BOSS, IDA, PAPALYMO = 3802315, 3802271, 3802272
local PACK_B = { { 4094218, 4094220 }, { 4094232, 4094233 }, { 4094474, 4094475 } }   -- 1440 hp, attack Ida and Papalymo
local PACK_A = { { 4094238, 4094239 }, { 4094240, 4094471 }, { 4094472, 4094473 } }   -- 300 hp, attack the player
local INTRO_FLAGS = { SceneFlags.NO_DEFAULT_CAMERA, SceneFlags.CONDITION_CUTSCENE, SceneFlags.SILENT_ENTER_TERRI_ENV,
  SceneFlags.HIDE_HOTBAR, SceneFlags.SILENT_ENTER_TERRI_BGM, SceneFlags.SILENT_ENTER_TERRI_SE, SceneFlags.DISABLE_STEALTH,
  0x00100000, SceneFlags.LOCK_HUD, SceneFlags.LOCK_HOTBAR, SceneFlags.DISABLE_CANCEL_EMOTE }

local function spawn(layoutId, hp, kind, hate)
  local npc = territory:spawnBNpc(layoutId, hp, kind)
  npc:setFlag(BNpcFlag.NoDeaggro)
  for _, h in hate do npc:hateAdd(h[1], h[2]) end
  return npc
end

function onCreate()
  for _, ref in EObjs do ref:spawn() end
end

function onStart()
  task.spawn(flow)
end

function onPlayerSetup(player)
  player:setPos(vector.create(323, -1.28, -320), 3)
end

function onEnterTerritory(player, eventId, param1, param2)
  player:playScene(director.id, 1, INTRO_FLAGS)
  player:setOnEnterEventDone(true)
end

function onDutyCommence(player)
  once("opening", function()
    local ida = spawn(IDA, 27780, "friendly", {})
    local papa = spawn(PAPALYMO, 27780, "friendly", {})
    local boss = spawn(BOSS, 21141, "enemy", { { ida, 10000 }, { papa, 10000 }, { player, 1 } })
    local adds = {}
    for _, id in PACK_B[1] do
      table.insert(adds, spawn(id, 1440, "enemy", { { player, 1 }, { ida, 10000 }, { papa, 10000 } }))
    end
    for _, ally in { ida, papa } do
      ally:hateAdd(boss, 10000)
      for _, add in adds do ally:hateAdd(add, 9999) end
    end
  end)
  director.data.commenced = true
end

-- returns false (after failing the duty) when the player died first
local function untilOrDead(pred)
  local function dead() local p = battle.player; return p ~= nil and not p.alive end
  -- hp, enemy counts and player death have no change notifications: poll
  wait.poll(function() return dead() or pred() end)
  if dead() then battle:fail(); return false end
  return true
end

function flow()
  wait.till(function() return director.data.commenced == true end)
  for wave, pct in { 75, 50, 25 } do
    local reached = untilOrDead(function()
      local boss = territory:bnpc(BOSS)
      local alliesUp = territory:bnpc(IDA) ~= nil and territory:bnpc(PAPALYMO) ~= nil
      return alliesUp and (if boss then boss.hpPercent else 0) <= pct
    end)
    if not reached then return end
    once("wave" .. wave, function()
      local ida, papa, player = territory:bnpc(IDA), territory:bnpc(PAPALYMO), battle.player
      if wave < 3 then
        for _, id in PACK_B[wave + 1] do spawn(id, 1440, "enemy", { { ida, 10000 }, { papa, 10000 } }) end
      end
      for _, id in PACK_A[wave] do spawn(id, 300, "enemy", { { player, 1 } }) end
    end)
  end
  if not untilOrDead(function() return battle.enemyCount == 0 end) then return end
  once("success", function() battle:success() end)
end

function onDutyComplete(player)
  local q = player:quest(battle.questId)
  if q.active then q.seq = 2 end
end
