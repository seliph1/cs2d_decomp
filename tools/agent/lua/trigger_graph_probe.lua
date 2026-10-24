-- WQ2D trigger graph probe: logs every trigger hook call (TG n name source) with the ms since start,
-- then at 20 s the number of entities in state 1 (TGS). Run on the original and on cs2d_server.exe, diff.
local n = 0
local t0 = os.clock()
function tg_trigger(name, source)
	n = n + 1
	print("TG " .. n .. " " .. name .. " " .. tostring(source))
end
addhook("trigger", "tg_trigger")
function tg_triggerentity(x, y)
	print("TGE " .. x .. " " .. y)
end
addhook("triggerentity", "tg_triggerentity")
local done = false
function tg_second()
	if done then return end
	if os.clock() - t0 >= 20 then
		done = true
		local c, list = 0, {}
		for _, e in pairs(entitylist()) do
			if entity(e.x, e.y, "state") then c = c + 1; list[#list + 1] = e.x .. "," .. e.y end
		end
		print("TGS state1=" .. c .. " triggers=" .. n)
		for _, s in ipairs(list) do print("TGL " .. s) end
	end
end
addhook("second", "tg_second")
