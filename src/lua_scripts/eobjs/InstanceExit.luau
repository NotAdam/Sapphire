script.eobj(2000139)

function onTalk(player, eobj, eventId)
  local res = player:playScene(eventId, 1, 0)
  if res.results[1] ~= 1 then player:exitInstance() end
end
