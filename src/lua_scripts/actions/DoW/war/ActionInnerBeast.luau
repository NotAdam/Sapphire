script.action(49)

function onExecute(action)
  local src, target, out = action.source, action.hitTarget, action.results
  if not src.isPlayer or not out then return end

  local defiance = src:statusEffect(ActionStatus.Defiance)
  if defiance then defiance:setModifier(ParamModifier.DamageDealtPercent, 0) end

  local dmg, kind = action:calcDamage(300)
  out:damage(src, target, dmg, kind)
  out:heal(target, src, dmg, CalcResultType.TypeRecoverHp, ActionResultFlag.EffectOnSource)
  out:applyStatusSelf(ActionStatus.InnerBeast, 15000, 0, { { ParamModifier.DamageTakenPercent, -20 } })

  if defiance and not src:hasStatusEffect(ActionStatus.Unchained) then
    defiance:setModifier(ParamModifier.DamageDealtPercent, -25)
  end
end
