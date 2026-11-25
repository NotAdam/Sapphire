function go()
  wait.poll(function() return flag == true end)
  done = 1
end
