--------------------------------------------------
-- Gun Game Script by Unreal Software           --
-- 11.03.2009 - www.UnrealSoftware.de           --
-- Adds Player Classes to your server           --
--------------------------------------------------

if sample==nil then sample={} end
sample.gg={}

-----------------------
-- INITIAL SETUP     --
-----------------------
function initArray(m)
	local array = {}
	for i = 1, m do
		array[i]=0
	end
	return array
end

-- Level
sample.gg.level=initArray(32)

-- Kill Counter
sample.gg.kills=initArray(32)

-- Weapons for different Levels
sample.gg.wpn={45,48,49,30,38,20,21,10,4,50}

-- Game Settings
parse("sv_gamemode 1")		-- Deathmatch
parse("mp_randomspawn 1")	-- Random Spawning
parse("mp_infammo 1")		-- Infinite Ammo


-----------------------
-- STARTROUND        --
-----------------------
addhook("startround","sample.gg.start")
function sample.gg.start()
	-- Reset Values
	local i
	for i=1,32 do
		sample.gg.level[i]=1
		sample.gg.kills[i]=0
		-- Reset Weapon (Kill)
		if (player(i,"exists")) then
			-- Strip All Weapons (0)
			parse("strip "..i.." 0")
			-- Give first Weapon
			parse("equip "..i.." "..sample.gg.wpn[1])
			-- Strip Knife
			parse("strip "..i.." 50")
		end
	end
end

-----------------------
-- JOIN              --
-----------------------
addhook("join","sample.gg.join")
function sample.gg.join(id)
	-- Reset
	sample.gg.level[id]=1
	sample.gg.kills[id]=0
end


-----------------------
-- SPAWN             --
-----------------------
addhook("spawn","sample.gg.spawn")
function sample.gg.spawn(id)
	-- Minimum Level is 1
	if (sample.gg.level[id]<1) then
		sample.gg.level[id]=1
	end
	-- Remove Knife
	if (sample.gg.level[id]<10) then
		parse("strip "..id.." 50")
	end
	-- Equip with Weapon for current Level
	if (sample.gg.level[id]>0 and sample.gg.level[id]<=10) then
		return sample.gg.wpn[sample.gg.level[id]]
	end
end

-----------------------
-- KILL              --
-----------------------
addhook("kill","sample.gg.kill")
function sample.gg.kill(id)
	-- Add Kill
	sample.gg.kills[id]=sample.gg.kills[id]+1
	-- Next Level?
	if (sample.gg.kills[id]>=3) then
		-- Reset Kills
		sample.gg.kills[id]=0
		-- Increase Level
		sample.gg.level[id]=sample.gg.level[id]+1
		-- Win?
		if (sample.gg.level[id]>=#sample.gg.wpn) then
			msg("©000255000"..player(id,"name").." has won the game!@C")
			parse("restart")
		else
			-- Give new Weapon
			parse("equip "..id.." "..sample.gg.wpn[sample.gg.level[id]])
			-- Remove last Weapon
			parse("strip "..id.." "..sample.gg.wpn[sample.gg.level[id]-1])
		end
	end
	-- Info
	if (sample.gg.level[id]<11) then
		msg2(id,"Level: "..sample.gg.level[id].." Kills: "..sample.gg.kills[id])
	end
end


-----------------------
-- NO BUYING         --
-----------------------
addhook("buy","sample.gg.buy")
function sample.gg.buy()
	return 1
end


-----------------------
-- NO COLLECTING     --
-----------------------
addhook("walkover","sample.gg.walkover")
function sample.gg.walkover(id,iid,type)
	if (type>=61 and type<=68) then
		return 0
	end
	return 1
end


-----------------------
-- NO DROPPING       --
-----------------------
addhook("drop","sample.gg.drop")
function sample.gg.drop()
	return 1
end


-----------------------
-- NO DEAD DROPPING --
-----------------------
addhook("die","sample.gg.die")
function sample.gg.die()
	return 1
end