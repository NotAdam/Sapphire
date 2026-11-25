function go()
  r1 = counter:add(2)
  counter.value = 5
  r2 = counter.value
  r3 = counter:mask({ 1, 4 })
end
