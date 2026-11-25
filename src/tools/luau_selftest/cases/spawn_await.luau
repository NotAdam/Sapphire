function go()
  local t = task.spawn(function()
    task.wait(1)
    return 7
  end)
  local ok, v = t:await()
  if ok then r = v end
end
