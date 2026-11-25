function go()
  local t = task.spawn(function() task.wait(10) end)
  t:cancel()
  local ok, err = t:await()
  okFlag = if ok then 1 else 0
  errIsCancelled = if err == task.CANCELLED then 1 else 0
end
