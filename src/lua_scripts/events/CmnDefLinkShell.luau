script.event(0xB0006)

local Action = { Create = 2, Rename = 3, Remove = 4 }
local NAME_ERROR = 0x15a

function onTalk(player, eventId, actorId)
  local res = player:playScene(eventId, 1, { SceneFlags.HIDE_HOTBAR, SceneFlags.NO_DEFAULT_CAMERA })
  local choice = res.results[1]

  if choice == Action.Create then
    player:playScene(eventId, 2, SceneFlags.HIDE_HOTBAR, nil, function(y)
      local err = if linkshell.create(y.string, player) then 0 else NAME_ERROR
      task.spawn(linkshell.finishAction, y.string, err, player, 1)   -- runs after the resume below is sent
      return { err }
    end)
  elseif choice == Action.Rename then
    player:playScene(eventId, 3, SceneFlags.HIDE_HOTBAR, nil, function(y)
      local err = if linkshell.rename(y.int, y.string, player) then 0 else NAME_ERROR
      task.spawn(linkshell.finishAction, y.string, err, player, 3)
      return { err }
    end)
  elseif choice == Action.Remove then
    player:playScene(eventId, 4, SceneFlags.HIDE_HOTBAR)
  end
end
