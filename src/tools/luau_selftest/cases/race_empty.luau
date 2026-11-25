function go()
  local ok, err = pcall(async.race, {})
  rejected = if not ok and string.find(tostring(err), "no functions", 1, true) then 1 else 0
end
