script.quest(66748)

local ID = 66748
local Seq = { Seq0 = 0, Seq1 = 1, Seq2 = 2, Seq3 = 3, Finish = 255 }
local Margeria, Emblyn, Recruit = 1000478, 1005530, 1005879

local function scene(player, n, flags)
  return player:playScene(ID, n, flags or SceneFlags.HIDE_HOTBAR)
end

function onTalk(quest, player, actorId)
  if actorId == Margeria then
    if quest.seq == Seq.Seq0 then
      if scene(player, 0).results[1] == 1 then   -- accepted
        scene(player, 1)
        quest.seq = Seq.Seq1
      end
    elseif quest.seq == Seq.Finish then
      if scene(player, 6).results[1] == 1 then player:finishQuest(ID) end
    end
  elseif actorId == Emblyn then
    if quest.seq == Seq.Seq1 then
      scene(player, 2)
      player:eventNotice(ID, 0, 0)
      quest.seq = Seq.Seq2
    elseif quest.seq == Seq.Seq2 then
      scene(player, 4)
    end
  elseif actorId == Recruit then
    if quest.seq == Seq.Seq3 then
      scene(player, 5)
      player:eventNotice(ID, 2, 0)
      quest.seq = Seq.Finish
    elseif quest.seq == Seq.Finish then
      scene(player, 7)
    end
  end
end

function onEnterTerritory(quest, player, param1, param2)
  if quest.seq ~= Seq.Seq2 then return end
  scene(player, 3, { SceneFlags.FADE_OUT, SceneFlags.CONDITION_CUTSCENE, SceneFlags.HIDE_UI })
  player:eventNotice(ID, 1, 0)
  quest.seq = Seq.Seq3
end
