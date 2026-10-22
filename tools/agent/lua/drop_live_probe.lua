-- Live drop/collect hook argument check (works with a password-protected server; driven by our
-- client: join, select slot 1 and press G). Gives the joining player a Laser (45).
addhook("join", "dl_join")
function dl_join(id)
    dl_id = id
    timer(800, "dl_setup")
end
function dl_setup()
    parse("maket "..dl_id)
    timer(800, "dl_spawn")
end
function dl_spawn()
    -- de_dust2 tile (78,45): walkable, the drop position measured in the live run
    parse("spawnplayer "..dl_id.." 2512 1456")
    timer(800, "dl_equip")
end
function dl_equip()
    parse("equip "..dl_id.." 45")
    print("DL equip laser to "..dl_id)
end
addhook("drop", "dl_drop")
function dl_drop(id, iid, typ, ammoin, ammo, mode, x, y)
    print("DL drop id="..tostring(id).." iid="..tostring(iid).." type="..tostring(typ)..
          " ammoin="..tostring(ammoin).." ammo="..tostring(ammo).." mode="..tostring(mode)..
          " x="..tostring(x).." y="..tostring(y))
    return 0
end
addhook("collect", "dl_collect")
function dl_collect(id, iid, typ, ain, a, mode)
    print("DL collect id="..tostring(id).." iid="..tostring(iid).." type="..tostring(typ)..
          " ain="..tostring(ain).." a="..tostring(a).." mode="..tostring(mode))
end
