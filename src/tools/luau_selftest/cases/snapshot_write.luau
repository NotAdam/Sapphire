function go()
  store.a = 1
  store.list = { 1, 2, { x = "y" } }
  store.v = vector.create(1, 2, 3)
  store.fn = function() end
end

function check()
  fnGone = if store.fn == nil then 1 else 0
end
