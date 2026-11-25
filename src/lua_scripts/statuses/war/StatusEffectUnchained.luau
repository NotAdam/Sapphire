script.status(ActionStatus.Unchained)

function onExpire(actor)
  local defiance = actor:statusEffect(ActionStatus.Defiance)
  if defiance then defiance:setModifier(ParamModifier.DamageDealtPercent, -25) end
end
