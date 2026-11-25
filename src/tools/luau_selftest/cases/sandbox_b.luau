seen = if g == nil then 1 else 0
hostHidden = if __host == nil and __wait == nil then 1 else 0

function go()
  local ok = pcall(function() task.wait = nil end)
  frozen = if ok then 0 else 1
end
