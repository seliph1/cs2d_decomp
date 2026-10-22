---@meta
--- CS2D Lua API definitions (version 1.0.1.8 p1, Lua 5.1.4).
--- Source: "sys/lua/info.txt", the in-repo help scrape "docs/cs2d_help_db.json"
--- (categories luacat/hookcat) and usage in the shipped scripts under sys/lua and bots.
--- This file is NOT executed; it only feeds the Lua language server so the shipped
--- scripts stop reporting undefined globals. Signatures are permissive on purpose:
--- most CS2D commands take a variable number of loosely typed arguments and return
--- either a value or a table depending on the first argument.

-- =========================================================================
-- Meta globals
-- =========================================================================

--- Set to true by "sys/lua/wrapper.lua" once the parse wrappers are loaded.
wrapper = false
--- Table holding the shipped sample scripts (see "sys/lua/server.lua").
sample = {}

-- =========================================================================
-- Basic / hook / timer
-- =========================================================================

---Attaches a Lua function to a CS2D hook.
---@param hook string  -- hook name, e.g. "join", "spawn", "say"
---@param func string  -- name of the Lua function to call
---@param priority? number  -- default 0, higher runs later and last return wins
function addhook(hook, func, priority) end

---Adds a client-side key bind that triggers the hook "key".
---@param key string
function addbind(key) end

---@param key string
---@param cmd string
function removebind(key, cmd) end
function removeallbinds() end
function freehook(hook, func) end

---Enables/disables a hook function without detaching it.
---@param hook string
---@param func string
---@param state boolean|integer
function sethookstate(hook, func, state) end

---Lists registered hook functions.
---@return table
function funcs() end

---@return any
function vars() end

---Calls a Lua function once after a delay in milliseconds.
---@param ms integer
---@param func string        -- function name
---@param ... any            -- extra args passed to the function
function timer(ms, func, ...) end

---@param id integer
function freetimer(id) end

-- =========================================================================
-- Images / tweening
-- =========================================================================

---@param id integer
---@param path string
---@param x number
---@param y number
---@param mode? integer
---@param ... any
---@return integer
function image(id, path, x, y, mode, ...) end

---@param id integer
function freeimage(id) end
function imagealpha(id, alpha) end
function imageblend(id, blend, alpha) end
function imagecolor(id, r, g, b) end
function imageframe(id, frame, ...) end
function imagehitzone(id, width, height, ...) end
function imageparam(id, param, ...) end
function imagepos(id, x, y, ...) end
function imagescale(id, xscale, yscale, ...) end

function tween_alpha(...) end
function tween_animate(...) end
function tween_color(...) end
function tween_frame(...) end
function tween_move(...) end
function tween_rotate(...) end
function tween_rotateconstantly(...) end
function tween_scale(...) end
function freeline(...) end
function freerect(...) end

-- =========================================================================
-- Game / map / tile
-- =========================================================================

---@param what string  -- e.g. "sv_gamemode", "sv_name", ...
---@return any
function game(what) end

---@param what string  -- property name, or number/0 to get a table of all maps
---@return any
function map(what) end

---@param x integer
---@param y integer
---@param what string  -- "tile"/"floor"/"wall"/"object"/"image"/... depending on use
---@return any
function tile(x, y, what) end

---@param tileframe integer
function tileproperty(tileframe) end

-- =========================================================================
-- Entity
-- =========================================================================

---Returns a value of the entity at tile position (x,y); no entity(0,"table").
---@param x integer
---@param y integer
---@param what string  -- "exists","type","typename","name","triggers","state","int0".."int9","str0".."str9","aistate"
---@return any
function entity(x, y, what) end

---@return table  -- list of entities, each with .x and .y
function entitylist() end
function inentityzone(id, x, y) end
function randomentity(...) end
function setentityaistate(x, y, state) end

-- =========================================================================
-- Objects
-- =========================================================================

---@param id integer  -- 0 to enumerate all objects as a table
---@param what string
---@return any
function object(id, what) end
function objectat(x, y, what) end
function objecttype(id) end
function closeobjects(x, y, type, dist) end

-- =========================================================================
-- Players
-- =========================================================================

---@param id integer  -- 0 to get a table (list) of all players
---@param what string
---@return any
function player(id, what) end
function playerammo(id, slot) end
function playerweapons(id) end
function closeplayers(id, dist) end
function hascloseplayers(id, dist) end
---@param id integer
---@return table
function closeitems(id, dist) end

-- =========================================================================
-- Items / projectiles / hostages
-- =========================================================================

---@param id integer
---@param what string
---@return any
function item(id, what) end
function itemtype(id) end

---@param id integer
---@param what string
---@return any
function projectile(id, what) end
function projectilelist() end

---@param id integer
---@param what string
---@return any
function hostage(id, what) end
function closehostage(id, dist) end
function randomhostage(...) end

-- =========================================================================
-- Misc commands
-- =========================================================================

function menu(id, ...) end
function msg(message) end
function msg2(id, message) end

---Executes an ordinary CS2D console command (the non-Lua command set).
---@param command string
function parse(command) end

function fow_in(id, ...) end
function checksumfile(path) end
function checksumstring(str) end
function reqcld(...) end
function reqhttp(...) end
function stats(...) end
function steamstats(...) end

-- =========================================================================
-- Physics stub
-- =========================================================================
physics = {}
function physics_platform_rectangle(...) end
function physics_platform_circle(...) end
function physics_platform_polygon(...) end
function physics_object_rectangle(...) end
function physics_object_circle(...) end
function physics_object_polygon(...) end
function physics_object_tire(...) end
function physics_object_tile(...) end
function physics_controller(...) end
function physics_setparent(...) end
function physics_setbounciness(...) end
function physics_setfriction(...) end
function physics_settorque(...) end
function physics_settexture(...) end
function physics_setmass(...) end
function physics_setforce(...) end
function physics_setlineardrag(...) end
function physics_setrotationaldrag(...) end
function physics_addforce(...) end
function physics_addtorque(...) end
function physics_clearforce(...) end
function physics_cleartorque(...) end
function physics_destroy(...) end
function physics_setattribute(...) end
function imagephysicsparent(...) end
function imageshadow(...) end


-- =========================================================================
-- AI commands (used by bots/*.lua)
-- =========================================================================

function ai_aim(...) end
function ai_attack(...) end
function ai_build(...) end
function ai_buy(id, type) end
function ai_debug(id, text) end
function ai_drop(id, item) end
function ai_findtarget(id, ...) end
function ai_freeline(id, x1, y1, x2, y2) end
---@return integer  -- 0 failed, 1 reached, 2 moved
function ai_goto(id, x, y) end
function ai_iattack(...) end
---@return integer  -- -1 blocked, 0 arrived, 1 moved
function ai_move(id, angle, ...) end
function ai_radio(id, radio) end
function ai_reload(id) end
function ai_respawn(id) end
function ai_rotate(id, angle) end
function ai_say(id, text) end
function ai_sayteam(id, text) end
function ai_selectweapon(id, slot) end
function ai_spray(id) end
function ai_use(id) end
