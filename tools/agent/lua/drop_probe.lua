-- Logs the exact drop / collect hook arguments. A bot drops its current weapon twice, then an
-- item is spawned on its tile to exercise collect.
function dp_fmt(id, iid, typ, ain, a, mode, x, y)
    return "DP id="..tostring(id).." iid="..tostring(iid).." type="..tostring(typ)..
           " ain="..tostring(ain).." a="..tostring(a).." mode="..tostring(mode)..
           " x="..tostring(x).." y="..tostring(y)
end
addhook("drop", "dp_drop")
function dp_drop(id, iid, typ, ammoin, ammo, mode, x, y)
    print(dp_fmt(id, iid, typ, ammoin, ammo, mode, x, y).." DROP")
    return 0
end
addhook("collect", "dp_collect")
function dp_collect(id, iid, typ, ain, a, mode)
    local carried = playerammo(id, typ)
    print(dp_fmt(id, iid, typ, ain, a, mode, nil, nil)..
          " COLLECT carried="..tostring(carried))
end
function dp_boot()
    parse("bot_add_t")
    timer(2500, "dp_drop1")
end
function dp_drop1()
    print("DP pos "..tostring(player(1,"x"))..","..tostring(player(1,"y")))
    parse("equip 1 45")
    timer(500, "dp_select")
end
function dp_select()
    ai_selectweapon(1, 45)
    timer(500, "dp_drop2")
end
function dp_drop2()
    ai_drop(1)
    timer(1500, "dp_pick")
end
function dp_pick()
    local x = math.floor(player(1,"x")/32)
    local y = math.floor(player(1,"y")/32)
    if x < 1 then x = 10 end
    if y < 1 then y = 10 end
    for dx = 2, 6 do
        if tile(x + dx, y, "walkable") then
            print("DP spawnitem 45 at "..(x + dx)..","..y)
            parse("spawnitem 45 "..(x + dx).." "..y)
            dp_tp_x, dp_tp_y = (x + dx) * 32 + 16, y * 32 + 16
            timer(500, "dp_tp")
            return
        end
    end
    print("DP no walkable tile")
end
function dp_tp()
    print("DP setpos "..dp_tp_x..","..dp_tp_y)
    parse("setpos 1 "..dp_tp_x.." "..dp_tp_y)
end
timer(2000, "dp_boot")
