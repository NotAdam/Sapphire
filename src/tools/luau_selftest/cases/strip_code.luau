function go()
  local function f() end
  store.plain = 1
  store.fnKey = { [f] = 1, keep = 2 }
  store.meta = setmetatable({ x = 1 }, { __index = function() return 0 end })
  store.ro = table.freeze({ f = f, y = 3 })
  store.roMeta = table.freeze(setmetatable({}, {}))
  store.co = coroutine.create(f)
end

function check()
  local n = 0
  for _ in store.fnKey do n += 1 end
  fnKeyGone = if n == 1 and store.fnKey.keep == 2 then 1 else 0
  metaGone = if getmetatable(store.meta) == nil and store.meta.x == 1 then 1 else 0
  roKept = if type(store.ro.f) == "function" and store.ro.y == 3 then 1 else 0
  roMetaKept = if getmetatable(store.roMeta) ~= nil then 1 else 0
  coGone = if store.co == nil then 1 else 0
  plainKept = if store.plain == 1 then 1 else 0
end
