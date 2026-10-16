-- Round probe + scripted death: kills the only player 14 s into the run to see how the original
-- dedicated reacts to a death while a team is empty (standard mode).
addhook("startround", "rk_start")
addhook("endround", "rk_end")
function rk_start(mode) print("ROUND start mode=" .. tostring(mode)) end
function rk_end(mode) print("ROUND end   mode=" .. tostring(mode)) end

function rk_sec()
    local ids = player(0, "table")
    local s = "SEC n=" .. #ids
    for i = 1, #ids do
        local id = ids[i]
        s = s .. " [id=" .. id .. " team=" .. tostring(player(id, "team")) ..
                " hp=" .. tostring(player(id, "health")) .. "]"
    end
    print(s)
end
timer(1000, "rk_sec", "", 0)

local n = 0
function rk_kill()
    n = n + 1
    if n == 14 and #player(0, "table") > 0 then
        print("KILL player 1")
        parse("killplayer 1")
    end
end
timer(1000, "rk_kill", "", 0)
