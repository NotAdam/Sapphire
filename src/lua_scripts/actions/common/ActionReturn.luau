script.action(ActionSkill.Return)

function onExecute(action)
  local src = action.source
  if src.isPlayer then src:teleport(src.homepoint, 3) end
end
