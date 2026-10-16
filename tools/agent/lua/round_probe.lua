-- Round restart cadence probe (standard mode, 0/1 players).
-- Run with the dedicated's -lua (the path is relative to sys/lua) and timestamp the printed lines
-- with tools/agent/round_timing.py, or capture the console log.
-- Original:  python tools/agent/round_timing.py 16 cs2d_dedicated.exe -offline -lua ../../tools/agent/lua/round_probe.lua
addhook("startround", "rp_start")
addhook("endround", "rp_end")
function rp_start(mode) print("ROUND start mode=" .. tostring(mode)) end
function rp_end(mode) print("ROUND end   mode=" .. tostring(mode)) end

-- One heartbeat per second: the player count and, per player, team / health / alive.
-- This is what tells the 0-player and 1-player states apart in a console log.
function rp_sec()
    local ids = player(0, "table")
    local s = "SEC n=" .. #ids
    for i = 1, #ids do
        local id = ids[i]
        s = s .. " [id=" .. id .. " team=" .. tostring(player(id, "team")) ..
                " hp=" .. tostring(player(id, "health")) .. "]"
    end
    print(s)
end
timer(1000, "rp_sec", "", 0)
