script.action(ActionSkill.Sprint)

function onExecute(action)
  local src = action.source
  if not src.isPlayer then return end
  local duration = math.floor(src.tp / 50) * 1000
  action.results:applyStatusSelf(ActionStatus.SprintStatus, duration, 30, nil, 0, true)
  src.tp = 0
end
