-- Live check of the fun_roleplay Trigger_Use at (275,154) ("rp_ticket"): puts the joining
-- player on the button tile and logs every usebutton + the button state.
addhook("join", "br_join")
function br_join(id)
    print("BR join "..id)
    timer(500, "br_team", tostring(id))
end
function br_team(id)
    parse("maket "..id)
    timer(1000, "br_spawn", id)
end
function br_spawn(id)
    parse("spawnplayer "..id.." 8816 4944")
    print("BR spawn "..id.." state="..tostring(entity(275,154,"state"))..
          " pos="..tostring(player(id,"x"))..","..tostring(player(id,"y"))..
          " hp="..tostring(player(id,"health")))
    timer(6000, "br_spawn2", id)
end
function br_spawn2(id)
    parse("spawnplayer "..id.." 8816 4944")
    print("BR spawn2 "..id.." state="..tostring(entity(275,154,"state"))..
          " pos="..tostring(player(id,"x"))..","..tostring(player(id,"y"))..
          " hp="..tostring(player(id,"health")))
end
addhook("usebutton", "br_button")
function br_button(id, x, y)
    print("BR usebutton id="..id.." "..x.." "..y.." state="..tostring(entity(x,y,"state")))
end
