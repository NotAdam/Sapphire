function go()
  local t = store
  for _ = 1, 40 do
    t.n = {}
    t = t.n
  end
  local shared = { v = 1 }
  store.a = shared
  store.b = shared
  store.none = require("./mod_nil")
  again = if require("./mod_nil") == nil then 1 else 0
end

function check()
  local depth, t = 0, store
  while t.n do
    depth += 1
    t = t.n
  end
  levels = depth
  alias = if store.a == store.b and store.a.v == 1 then 1 else 0
end
