-- Measures the Lua player() mouse properties on the original dedicated server.
function mouse_probe_tick()
    for _, id in ipairs(player(0, "tableliving")) do
        print(string.format(
            "MOUSEPROBE id=%d x=%s y=%s rot=%s mapx=%s mapy=%s mx=%s my=%s dist=%s sw=%s sh=%s",
            id,
            tostring(player(id, "x")), tostring(player(id, "y")),
            tostring(player(id, "rot")),
            tostring(player(id, "mousemapx")), tostring(player(id, "mousemapy")),
            tostring(player(id, "mousex")), tostring(player(id, "mousey")),
            tostring(player(id, "mousedist")),
            tostring(player(id, "screenw")), tostring(player(id, "screenh"))))
    end
end
addhook("second", "mouse_probe_tick")
