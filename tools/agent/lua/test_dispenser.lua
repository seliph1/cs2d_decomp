local bot_id = 0
local ticks = 0
local items_prev = 0

addhook("spawn", "on_spawn")
addhook("ms100", "on_ms100")

function on_spawn(id)
    bot_id = id
    print("BOT SPAWNED id=" .. id)
    parse("setpos " .. id .. " 500 500")
    parse("sethealth " .. id .. " 50")
    parse("setmoney " .. id .. " 0")
    -- Spawn Dispenser (7) at tile 16, 15 (near 500, 500)
    parse("spawnobject 7 16 15 0 0 1 0")
    -- Spawn Supply (9) at tile 14, 15
    parse("spawnobject 9 14 15 0 0 1 0")
end

function on_ms100()
    if bot_id > 0 then
        ticks = ticks + 1
        local hp = player(bot_id, "health")
        local money = player(bot_id, "money")
        local itbl = item(0, "table")
        local nitems = #itbl
        if ticks % 5 == 0 or nitems ~= items_prev then
            print(string.format("PROBE ms100 tick=%d: hp=%d money=%d items=%d", ticks, hp, money, nitems))
            items_prev = nitems
        end
        if ticks >= 60 then
            parse("quit")
        end
    end
end

parse("sv_gamemode 3")
parse("bot_freeze 1")
parse("bot_add_t")

