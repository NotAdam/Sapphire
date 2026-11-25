function go()
  local base = counter.value
  native = counter:add(1) - base              -- a native method, called straight from __namecall
  closure = counter.add(counter, 1) - base    -- the same method through its closure

  local ok, err = pcall(function() return counter:nope() end)
  unknown = if not ok and string.find(tostring(err), "no method 'nope'", 1, true) then 1 else 0
  ok, err = pcall(function() return counter:add("x") end)
  named = if not ok and string.find(tostring(err), "to 'add'", 1, true) then 1 else 0

  waited = counter:waitAdd(1) - base          -- implemented in Luau; yields inside the method call
  resumed = counter:pause()                   -- a raw method that yields; resumed by the host with a value
end
