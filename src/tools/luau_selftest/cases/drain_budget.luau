-- two tasks hand a turn back and forth through wait.till: each step's end wakes the other, so the ready queue never
-- empties. CAP only bounds the loop when the drain budget is broken
CAP = 200000
n = 0
turn = 1

local function player(me, other)
  while n < CAP do
    wait.till(function() return turn == me end)
    turn = other
    n += 1
  end
end

function go()
  task.spawn(player, 1, 2)
  task.spawn(player, 2, 1)
end
