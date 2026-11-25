-- exit callbacks run while the context closes: tasks they start would outlive it
function go()
  task.onExit(function()
    local ok, err = pcall(task.spawn, function() task.wait(math.huge) end)
    store.spawnRejected = if not ok and string.find(tostring(err), "being unloaded", 1, true) then 1 else 0
  end)
  task.onExit(function()
    task.delay(0, function() end)
  end)
  task.wait(math.huge)
end
