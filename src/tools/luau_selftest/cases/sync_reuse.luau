function hook(n) count = (count or 0) + n end
function bad() error("boom") end
function yielder() task.wait(1) end
