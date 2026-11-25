-- wait.till wakes when another step of the same script ran (no host notification, no polling)
function waiter()
  wait.till(function() return ready == true end)
  done = 1
end

-- never satisfied: must re-check without waking the other waiters in a loop
function idler()
  wait.till(function() return never == true end)
end

function setter() ready = true end
