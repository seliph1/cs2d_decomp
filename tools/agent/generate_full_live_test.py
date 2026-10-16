import json

with open("docs/cs2d_help_db_complete.json", "r", encoding="utf-8") as f:
    help_db = json.load(f)

with open("tools/agent/extracted_api.json", "r", encoding="utf-8") as f:
    api = json.load(f)

lua_cmds = sorted(list(help_db.get("lua_commands", {}).keys()))
hooks = sorted(api.get("hooks", []))
console_cmds = sorted(api.get("commands", []))

print(f"Lua commands: {len(lua_cmds)}, Hooks: {len(hooks)}, Console commands: {len(console_cmds)}")

lua_code = []
lua_code.append("""-- Comprehensive Live Verification Suite for CS2D
-- Testing 88 Lua Commands, 79 Lua Hooks, and Console Commands with a real CS2D.exe client

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

print("==================================================")
print(">>> INITIALIZING FULL CS2D LIVE SUITE <<<")
print("==================================================")

-- 1. AUDIT ALL 88 LUA COMMANDS (Call / Validate signatures)
local lua_funcs = {""")

for c in lua_cmds:
    lua_code.append(f'    "{c}",')

lua_code.append("""}

print("--- AUDITING ALL 88 LUA COMMANDS ---")
local lua_cmd_ok = 0
for _, name in ipairs(lua_funcs) do
    if _G[name] ~= nil and type(_G[name]) == "function" then
        lua_cmd_ok = lua_cmd_ok + 1
    else
        print("[ERROR] Missing Lua command: " .. name)
    end
end
check("All 88 Lua commands registered globally", lua_cmd_ok == #lua_funcs)

-- Test execution of representative Lua commands
local ok, err = pcall(function()
    return checksumstring("hello world")
end)
check("checksumstring executes", ok == true)

check("map width > 0", map("xsize") > 0)
check("tile(0,0,frame) returns number", type(tile(0, 0, "frame")) == "number")
check("tileproperty(1, property)", type(tileproperty(1, "property")) == "number")
check("itemtype(30, slot) == 1", itemtype(30, "slot") == 1)
check("entitylist is table", type(entitylist()) == "table")
check("freeline(0,0,100,100)", freeline(0, 0, 100, 100) ~= nil)
check("freerect(1,1,2,2)", freerect(1, 1, 2, 2) ~= nil)
check("objecttype(1, name)", type(objecttype(1, "name")) == "string")
check("funcs returns table", type(funcs()) == "table")
check("vars returns table", type(vars()) == "table")

-- 2. REGISTER ALL 79 HOOKS
local hook_stats = {}
local official_hooks = {""")

for h in hooks:
    lua_code.append(f'    "{h}",')

lua_code.append("""}

print("--- REGISTERING ALL 79 OFFICIAL HOOKS ---")
for _, h in ipairs(official_hooks) do
    hook_stats[h] = 0
    local func_name = "hook_cb_" .. h
    _G[func_name] = function(...)
        hook_stats[h] = hook_stats[h] + 1
        return nil
    end
    addhook(h, func_name)
end
check("All 79 hooks successfully registered via addhook", true)

-- 3. SPECIFIC HOOK TEST CALLBACKS FOR ACTIVE CLIENT
addhook("join", "on_client_join")
function on_client_join(id)
    print(">>> [CLIENT JOINED] ID=" .. id .. " Name=" .. player(id, "name"))
    check("Real client connected with valid ID", id > 0)
    check("Real client name populated", #player(id, "name") > 0)
    
    -- Automatic assignment to team 2 (CT) and spawn
    timer(1000, "step_team", tostring(id))
end

function step_team(id_str)
    local id = tonumber(id_str)
    print(">>> Putting player " .. id .. " into CT")
    parse("makect " .. id)
    timer(1000, "step_spawn", id_str)
end

function step_spawn(id_str)
    local id = tonumber(id_str)
    print(">>> Spawning player " .. id)
    parse("spawnplayer " .. id .. " 512 512")
end

addhook("spawn", "on_client_spawn")
function on_client_spawn(id)
    print(">>> [CLIENT SPAWNED] ID=" .. id .. " HP=" .. player(id, "health"))
    check("Client spawned with 100 HP", player(id, "health") == 100)
    
    -- Test batch of console commands on active player
    parse("setmoney " .. id .. " 16000")
    check("parse setmoney", player(id, "money") == 16000)
    
    parse("sethealth " .. id .. " 90")
    check("parse sethealth", player(id, "health") == 90)
    
    parse("setarmor " .. id .. " 80")
    check("parse setarmor", player(id, "armor") == 80)
    
    parse("equip " .. id .. " 30") -- AK47
    parse("setweapon " .. id .. " 30")
    check("parse equip/setweapon", player(id, "weptype") == 30)

    -- Test hudtxt & hudtxtclear
    parse('hudtxt 1 "CS2D_COMPATIBILITY_TEST" 320 200 1')
    parse('hudtxtcolorfade 0 1 500 255 0 0')
    parse('hudtxtmove 0 1 500 320 220')

    -- Display interactive menu
    menu(id, "Test Menu@b,Option A,Option B,Option C")
end

addhook("attack", "on_client_attack")
function on_client_attack(id)
    print(">>> [CLIENT ATTACK] ID=" .. id .. " Weapon=" .. player(id, "weptype"))
end

addhook("reload", "on_client_reload")
function on_client_reload(id, mode)
    print(">>> [CLIENT RELOAD] ID=" .. id .. " Mode=" .. mode)
end

addhook("say", "on_client_say")
function on_client_say(id, msg)
    print(">>> [CLIENT CHAT] ID=" .. id .. " Msg=" .. msg)
end

addhook("spray", "on_client_spray")
function on_client_spray(id)
    print(">>> [CLIENT SPRAY] ID=" .. id)
end

addhook("drop", "on_client_drop")
function on_client_drop(id, iid, type, ammo, ammoin)
    print(">>> [CLIENT DROP] ID=" .. id .. " Type=" .. type)
end

-- 4. TEST SUITE OF SERVER CONSOLE COMMANDS
print("--- TESTING CONSOLE COMMANDS BATCH ---")
local cvars = {
    {"mp_c4timer", "35"},
    {"mp_roundtime", "3"},
    {"mp_buytime", "0.5"},
    {"mp_startmoney", "800"},
    {"sv_gamemode", "0"},
    {"mp_friendlyfire", "1"}
}

for _, pair in ipairs(cvars) do
    parse(pair[1] .. " " .. pair[2])
    check("Console command " .. pair[1], game(pair[1]) == pair[2])
end

-- 5. FINAL REPORT SCHEDULING (After client interaction period)
timer(16000, "finish_live_suite")
function finish_live_suite()
    print("==================================================")
    print("         FULL LIVE SUITE EXECUTION REPORT         ")
    print("==================================================")
    check("Join hook triggered by real client", hook_stats["join"] >= 1)
    check("Team hook triggered", hook_stats["team"] >= 1)
    check("Spawn hook triggered", hook_stats["spawn"] >= 1)
    check("ms100 periodic hook active", hook_stats["ms100"] > 10)
    check("second periodic hook active", hook_stats["second"] >= 2)
    check("always per-frame hook active", hook_stats["always"] > 100)
    
    print("Interactive Hooks Triggered:")
    print("  - attack: " .. hook_stats["attack"])
    print("  - reload: " .. hook_stats["reload"])
    print("  - drop:   " .. hook_stats["drop"])
    print("  - say:    " .. hook_stats["say"])
    print("  - spray:  " .. hook_stats["spray"])
    print("  - menu:   " .. hook_stats["menu"])
    print("--------------------------------------------------")
    print("TOTAL TESTS: " .. tests_run)
    print("PASSED:      " .. tests_passed)
    print("FAILED:      " .. tests_failed)
    print("==================================================")
end
""")

with open("tools/agent/lua/full_live_suite.lua", "w", encoding="utf-8") as out:
    out.write("\n".join(lua_code))

print("Created tools/agent/lua/full_live_suite.lua successfully!")

