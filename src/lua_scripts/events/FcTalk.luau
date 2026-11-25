script.event(0x001F0000)

function onTalk(player, eventId, actorId)
  local cases = exd.switchTalk(eventId)
  if not cases then return end

  local talk = 0
  for i = 16, 1, -1 do
    local case = cases[i]
    if bit32.rshift(case.condition, 16) == EventHandlerType.Quest and player:isQuestCompleted(case.condition) then
      talk = case.talk
      break
    else
      talk = cases[1].talk
    end
  end
  if talk == 0 then return end

  player:nest(actorId, talk, function(child)
    child:playScene(0, { SceneFlags.HIDE_HOTBAR, SceneFlags.NO_DEFAULT_CAMERA }, { 0 })
  end, { param2 = 5 })
end
