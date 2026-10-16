-- tools/agent/lua/live_interactive_suite.lua
-- Comprehensive verification of Console Commands, Lua Hooks, and Lua Functions
-- executed while interacting with a real CS2D client.

local tests_run = 0
local tests_passed = 0
local tests_failed = 0

local function check(desc, cond)
    tests_run = tests_run + 1
    if cond then
        tests_passed = tests_passed + 1
        print("[TEST_PASS] " .. desc)
    else
        tests_failed = tests_failed + 1
        print("[TEST_FAIL] " .. desc)
    end
end

print(">>> LIVE INTERACTIVE SUITE INITIALIZED <<<")

-- 1. Test Console Commands via parse() & cvar system
parse("mp_c4timer 45")
check("Console cvar mp_c4timer", game("mp_c4timer") == "45")

-- 2. Register key hooks to observe real client actions
local hooks_triggered = {
    join = 0,
    team = 0,
    spawn = 0,
    say = 0,
    attack = 0,
    reload = 0,
    select = 0,
    drop = 0,
    minute = 0,
    second = 0,
    ms100 = 0,
    always = 0,
    menu = 0,
    log = 0
}

addhook("join", "on_test_join")
function on_test_join(id)
    hooks_triggered.join = hooks_triggered.join + 1
    print(">>> HOOK JOIN: id=" .. id .. " name=" .. player(id, "name") .. " ip=" .. player(id, "ip"))
    check("player exists on join", player(id, "exists") == true)
    check("player name not empty", #player(id, "name") > 0)
    check("player weapons is table", type(playerweapons(id)) == "table")

    -- Automatically put joining player on CT and spawn them after short delay
    timer(1000, "join_put_team", tostring(id))
end

function join_put_team(id_str)
    local id = tonumber(id_str)
    print(">>> CALLING makect " .. id)
    parse("makect " .. id)
    timer(1000, "join_spawn_player", id_str)
end

function join_spawn_player(id_str)
    local id = tonumber(id_str)
    print(">>> CALLING spawnplayer " .. id)
    parse("spawnplayer " .. id .. " 512 512")
end

addhook("team", "on_test_team")
function on_test_team(id, team, look)
    hooks_triggered.team = hooks_triggered.team + 1
    print(">>> HOOK TEAM: id=" .. id .. " team=" .. team .. " look=" .. look)
    check("team hook valid team", team == 1 or team == 2 or team == 0)
end

addhook("spawn", "on_test_spawn")
function on_test_spawn(id)
    hooks_triggered.spawn = hooks_triggered.spawn + 1
    print(">>> HOOK SPAWN: id=" .. id .. " health=" .. player(id, "health") .. " pos=" .. player(id, "x") .. "," .. player(id, "y"))
    check("player health 100 on spawn", player(id, "health") == 100)
    check("player alive on spawn", player(id, "alive") == true)
    
    -- Execute console commands on player
    parse("sethealth " .. id .. " 85")
    check("parse sethealth", player(id, "health") == 85)
    
    parse("setarmor " .. id .. " 75")
    check("parse setarmor", player(id, "armor") == 75)
    
    parse("setscore " .. id .. " 10")
    check("parse setscore", player(id, "score") == 10)

    parse("equip " .. id .. " 30") -- AK-47
    parse("setweapon " .. id .. " 30")
    check("parse equip AK47", player(id, "weptype") == 30)

    -- Test hudtxt
    parse('hudtxt 1 "LIVE_TEST_ACTIVE" 320 200 1')

    -- Open menu to test menu interaction
    menu(id, "Interactive Test Menu,Option 1|Test,Option 2|Confirm")
end

addhook("menu", "on_test_menu")
function on_test_menu(id, title, button)
    hooks_triggered.menu = hooks_triggered.menu + 1
    print(">>> HOOK MENU: id=" .. id .. " title=" .. title .. " button=" .. button)
    check("menu hook title match", title == "Interactive Test Menu")
end

addhook("say", "on_test_say")
function on_test_say(id, msg)
    hooks_triggered.say = hooks_triggered.say + 1
    print(">>> HOOK SAY: id=" .. id .. " msg=" .. msg)
    check("say hook received message", #msg > 0)
end

addhook("attack", "on_test_attack")
function on_test_attack(id)
    hooks_triggered.attack = hooks_triggered.attack + 1
    print(">>> HOOK ATTACK: id=" .. id .. " weapon=" .. player(id, "weptype"))
end

addhook("reload", "on_test_reload")
function on_test_reload(id, mode)
    hooks_triggered.reload = hooks_triggered.reload + 1
    print(">>> HOOK RELOAD: id=" .. id .. " mode=" .. mode)
end

addhook("select", "on_test_select")
function on_test_select(id, weptype, mode)
    hooks_triggered.select = hooks_triggered.select + 1
    print(">>> HOOK SELECT: id=" .. id .. " weptype=" .. weptype .. " mode=" .. mode)
end

addhook("drop", "on_test_drop")
function on_test_drop(id, itemid, type, ammo, ammoin)
    hooks_triggered.drop = hooks_triggered.drop + 1
    print(">>> HOOK DROP: id=" .. id .. " item=" .. type)
end

addhook("ms100", "on_test_ms100")
function on_test_ms100()
    hooks_triggered.ms100 = hooks_triggered.ms100 + 1
end

addhook("second", "on_test_second")
function on_test_second()
    hooks_triggered.second = hooks_triggered.second + 1
end

addhook("always", "on_test_always")
function on_test_always()
    hooks_triggered.always = hooks_triggered.always + 1
end

addhook("log", "on_test_log")
function on_test_log(txt)
    hooks_triggered.log = hooks_triggered.log + 1
end

-- 3. World / Geometry queries test
check("map x dimension", map("xsize") > 0)
check("map y dimension", map("ysize") > 0)
check("tile 0 0 exists", type(tile(0, 0, "frame")) == "number")
check("tileproperty 1", type(tileproperty(1, "property")) == "number")
check("itemtype AK47 slot", itemtype(30, "slot") == 1)
check("entitylist is table", type(entitylist()) == "table")
check("freeline test", freeline(0, 0, 100, 100) ~= nil)

-- 4. Timer to report final results
timer(16000, "report_live_suite")
function report_live_suite()
    print("==================================================")
    print("       LIVE INTERACTIVE TEST SUITE RESULTS        ")
    print("==================================================")
    check("Hook join called", hooks_triggered.join >= 1)
    check("Hook team called", hooks_triggered.team >= 1)
    check("Hook spawn called", hooks_triggered.spawn >= 1)
    check("Hook ms100 called", hooks_triggered.ms100 > 10)
    check("Hook second called", hooks_triggered.second >= 1)
    check("Hook always called", hooks_triggered.always > 50)
    check("Hook log called", hooks_triggered.log > 0)
    print("Hook attack calls: " .. hooks_triggered.attack)
    print("Hook say calls: " .. hooks_triggered.say)
    print("Hook reload calls: " .. hooks_triggered.reload)
    print("Hook menu calls: " .. hooks_triggered.menu)
    print("--------------------------------------------------")
    print("Tests Passed: " .. tests_passed .. " / " .. tests_run .. " (Failed: " .. tests_failed .. ")")
    print("==================================================")
end

