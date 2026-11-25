function go()
  coroutine.wrap(function() task.wait(1) end)()
end

-- a task thread obtained through coroutine.running() cannot be resumed or closed by script code
function resumeSelf()
  local me = coroutine.running()
  task.spawn(function()
    local ok, err = pcall(coroutine.resume, me)
    resumeRejected = if not ok and string.find(tostring(err), "task thread") then 1 else 0
    local ok2, err2 = pcall(coroutine.close, me)
    closeRejected = if not ok2 and string.find(tostring(err2), "task thread") then 1 else 0
  end)
  task.wait(0)
  resumedBack = 1
end
