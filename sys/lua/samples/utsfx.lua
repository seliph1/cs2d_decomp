--------------------------------------------------
-- UT+Quake Sounds Script by Unreal Software    --
-- 22.02.2009 - www.UnrealSoftware.de           --
-- Adds UT and Quake Sounds to your Server      --
--------------------------------------------------

if sample==nil then sample={} end
sample.ut={}

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
sample.ut.timer=initArray(32)				-- time of last kill, for each player
sample.ut.level=initArray(32)				-- current kill level (killstreak), for each player
sample.ut.fblood=0							-- first blood already shed? 0=no/1=yes


-----------------------
-- PREPARE TO FIGHT! --
-----------------------
addhook("startround","sample.ut.startround")
function sample.ut.startround()
	parse("sv_sound \"fun/prepare.wav\"")
	sample.ut.fblood=0
end


-----------------------
-- KILL SOUNDS+MSGS  --
-----------------------
addhook("kill","sample.ut.kill")
function sample.ut.kill(killer,victim,weapon)
	-- Was last kill more than 3 secs ago?
	if (os.clock()-sample.ut.timer[killer])>3 then
		-- Yes, more than 3 secs ago! Reset level!
		sample.ut.level[killer]=0;
	end
	-- Get level of player and increase it
	level=sample.ut.level[killer]
	level=level+1
	sample.ut.level[killer]=level
	-- Reset the timer (last kill happened now!)
	sample.ut.timer[killer]=os.clock()
	-- FIRST BLOOD?
	if (sample.ut.fblood==0) then
		sample.ut.fblood=1
		parse("sv_sound \"fun/firstblood.wav\"");
		msg (player(killer,"name").." sheds FIRST BLOOD by killing "..player(victim,"name").."!")
	end
	-- HUMILIATION? (KNIFEKILL)
	if (weapon==50) then
		-- HUMILIATION!
		parse("sv_sound \"fun/humiliation.wav\""); 
		msg (player(killer,"name").." humiliated "..player(victim,"name").."!")
	else
		-- REGULAR KILL
		if (level==1) then
			-- 1: Single Kill! Nothing Special!
		elseif (level==2) then
			-- 2: Doublekill
			parse("sv_sound \"fun/doublekill.wav\"");
			msg (player(killer,"name").." made a Doublekill!")
		elseif (level==3) then
			-- 3: Multikill
			parse("sv_sound \"fun/multikill.wav\"")
			msg (player(killer,"name").." made a Multikill!")
		elseif (level==4) then
			-- 4: Ultrakill
			parse("sv_sound \"fun/ultrakill.wav\"")
			msg (player(killer,"name").." made an ULTRAKILL!")
		elseif (level==5) then
			-- 5: Monsterkill
			parse("sv_sound \"fun/monsterkill.wav\"")
			msg (player(killer,"name").." made a MO-O-O-O-ONSTERKILL-ILL-ILL!")
		else
			-- >5: Unstoppable
			parse("sv_sound \"fun/unstoppable.wav\"")
			msg (player(killer,"name").." is UNSTOPPABLE! "..level.." KILLS!")
		end
	end
end