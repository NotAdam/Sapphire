calls = 0
rebound = 0
function onTalk() calls += 1 end
function rebind()
  onTalk = function() rebound += 1 end
end
