-- settile probe: changes tiles before anyone joins (join snapshot [252,7,9]) and after a join ([62]),
-- including a tile set back to its map frame. Output lines start with ST.
local function st_dump(x, y)
	local function q(k)
		local ok, v = pcall(tile, x, y, k)
		if ok then return tostring(v) end
		return "?"
	end
	print("ST "..x.." "..y.." frame="..q("frame").." orig="..q("originalframe").." custom="..q("hascustomframe")
		.." rot="..q("rot").." blend="..q("blend").." prop="..q("property"))
end
st_orig = 0
function st_pre()
	st_orig = tile(10, 10, "frame")
	print("ST orig 10 10 = "..st_orig)
	parse("settile 10 10 5")
	parse("settile 11 10 6 1 255 0 0")
	parse("settile 12 10 7 2 255 255 255 3 left 2")
	parse("settile 13 10 8 3 0 128 255 4 topright 1")
	parse("settile 14 10 9 0 255 255 255 0 top 1")
	for x = 10, 14 do st_dump(x, 10) end
end
timer(1000, "st_pre")
addhook("join", "st_join")
function st_join(id)
	timer(4000, "st_live")
end
function st_live()
	parse("settile 15 10 10")
	parse("settile 16 10 11 1 10 20 30 5 bottom 3")
	parse("settile 17 10 12 2 255 255 255 0 bottomleft 5")
	parse("settile 10 10 "..st_orig)
	print("ST live")
	for x = 10, 17 do st_dump(x, 10) end
end
