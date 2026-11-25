local a1 = require("./mod_a")
local a2 = require("./mod_a")
same = if a1 == a2 and a1.value == 42 then 1 else 0

local ok, err = pcall(require, "./mod_cycle_a")
cycle = if not ok and string.find(tostring(err), "circular require", 1, true) then 1 else 0

-- src/lua_scripts/common.luau exists, but lies outside this directory (the runtime's root)
ok, err = pcall(require, "../../../lua_scripts/common")
outside = if not ok and string.find(tostring(err), "outside the scripts directory", 1, true) then 1 else 0
