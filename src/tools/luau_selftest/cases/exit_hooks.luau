-- an exit callback that registers itself again must not recurse without bound
function nest()
  local function f() task.onExit(f) end
  task.onExit(f)
end

-- an exit callback cancelling its own (already exiting) task is a no-op
function reenter()
  local t
  t = task.spawn(function()
    task.onExit(function() t:cancel() end)
    task.wait(10)
  end)
  task.wait(0)
  t:cancel()
  reentered = 1
end

-- the async helpers leave no exit hooks behind on the calling thread
function hooks()
  for i = 1, 20 do
    async.timeout(5, function() return i end)
    async.all({ function() return i end })
  end
  hooksDone = 1
  task.wait(100)
end

-- cancelling the waiting thread cancels its subtasks
function waiter()
  async.all({ function() task.wait(100) end, function() task.wait(100) end })
end
