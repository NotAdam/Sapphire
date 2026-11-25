function go()
  -- task.onExit returns a removable hook
  local t = task.spawn(function()
    local h = task.onExit(function() exitRan = 1 end)
    h:remove()
    h:remove()
    task.wait(10)
  end)
  task.wait(0)
  t:cancel()
  exitRan = exitRan or 0
  local ok, err = t:await()
  cancelledSentinel = if not ok and err == task.CANCELLED then 1 else 0

  -- a hook can be removed from another thread
  local hook
  local t2 = task.spawn(function()
    hook = task.onExit(function() exitRan2 = 1 end)
    task.wait(10)
  end)
  task.wait(0)
  hook:remove()
  t2:cancel()
  exitRan2 = exitRan2 or 0

  -- a failing task.delay callback is named in the error
  local function exploding() error("kaboom") end
  task.delay(0, exploding)
end
