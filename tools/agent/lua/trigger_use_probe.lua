-- Trigger_Use probe: dumps every int of the buttons we care about (fun_roleplay) and their state.
-- Run with the dedicated `-lua ../../tools/agent/lua/trigger_use_probe.lua`.
function tu_dump()
    local spots = { {275,154}, {57,75}, {56,73}, {18,5}, {9,131} }
    for _, s in ipairs(spots) do
        local x, y = s[1], s[2]
        if entity(x, y, "exists") then
            local line = "TU "..x.." "..y..
                " type="..tostring(entity(x,y,"typename"))..
                " name='"..tostring(entity(x,y,"name")).."'"..
                " trigger='"..tostring(entity(x,y,"trigger")).."'"..
                " state="..tostring(entity(x,y,"state"))
            for i = 0, 9 do
                line = line.." i"..i.."="..tostring(entity(x,y,"int"..i))
            end
            print(line)
        else
            print("TU "..x.." "..y.." none")
        end
    end
end
timer(5000, "tu_dump")
