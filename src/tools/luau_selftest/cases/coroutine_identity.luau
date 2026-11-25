-- created by the load thread, which has finished by the time go() resumes it
local gen = coroutine.wrap(function()
  while true do coroutine.yield(task.clock()) end
end)

function go()
  clock = gen()   -- task.clock needs the context of the task that is running
end
