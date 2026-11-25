local function rejected(v)
  return if pcall(function() return counter:byte(v) end) then 0 else 1
end

function go()
  truncated = counter:byte(255.9)
  tooBig = rejected(256)
  negative = rejected(-1)
  nan = rejected(0 / 0)
end
