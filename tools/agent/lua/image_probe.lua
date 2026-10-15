-- image / NPC probe: Lua images in every draw mode, their setters and tweens, and the five NPC types next to the
-- joining player. Output lines start with IM.
function im_spawn(id)
	id = tonumber(id)
	local x, y = player(id, "x"), player(id, "y")
	local tx, ty = player(id, "tilex"), player(id, "tiley")
	local a = image("gfx/sprites/flare2.bmp", x + 64, y, 0)          -- floor
	imagecolor(a, 255, 0, 0)
	imageblend(a, 1)
	local b = image("gfx/cs2d.bmp", x - 96, y - 64, 1)               -- top
	imagescale(b, 0.5, 0.5)
	imagealpha(b, 0.6)
	tween_rotateconstantly(b, 2)
	local c = image("gfx/hud_arrow.bmp", 425, 60, 2)                 -- HUD
	tween_move(c, 3000, 425, 200)
	local d = image("gfx/sprites/flare1.bmp", 0, 0, 200 + id)        -- follows the player
	imagecolor(d, 0, 255, 0)
	imageblend(d, 1)
	local e = image("<tile:5>", x + 128, y - 64, 3)                  -- super top, tile 5
	local f = image("<flag:br>", x + 160, y - 64, 1)                 -- flag
	imagescale(f, 2, 2)
	local g = image("<avatar_1>", x + 200, y - 64, 1)                -- avatar placeholder
	local h = image("<spritesheet:gfx/hud_nums.bmp:48:66>", x + 120, y + 40, 1)
	tween_animate(h, 200, 0)
	local k = image("gfx/cs2d.bmp", x - 160, y + 80, 4)              -- background (under the tiles)
	local l = image("<light>", x, y - 96, 1)
	local m = image("gfx/hud_arrow.bmp", 1, 0, 132 + id)            -- over walls, turns with the player
	for t = 1, 5 do
		parse("spawnnpc " .. t .. " " .. (tx - 3 + t) .. " " .. (ty + 3) .. " 0")
	end
	print("IM spawn " .. a .. " " .. b .. " " .. c .. " " .. d .. " " .. e .. " " .. f .. " " .. g .. " " .. h .. " " .. k .. " " .. l .. " " .. m)
end
addhook("join", "im_join")
function im_join(id)
	timer(500, "im_team", tostring(id))
end
function im_team(id)
	parse("maket " .. id)
	timer(2500, "im_spawn", id)
end
-- Keeps the player alive next to the NPCs
addhook("hit", "im_hit")
function im_hit()
	return 1
end
