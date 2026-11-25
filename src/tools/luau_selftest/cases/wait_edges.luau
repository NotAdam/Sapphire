function nanWait() task.wait(0 / 0); nanDone = 1 end
function negWait() task.wait(-1); negDone = 1 end
function hugeWait() task.wait(math.huge); hugeDone = 1 end
-- fits a uint64 in ms, but now + delay does not
function overflowWait() task.wait(1.8446744073709e16); overflowDone = 1 end
function farWait() task.wait(1e9); farDone = 1 end
