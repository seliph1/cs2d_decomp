--------------------------------------------------
-- Some Say Functions by Unreal Software        --
-- 22.02.2009 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.sayfuncs={}

--------------------------------------
-- SAY FUNCTIONS                    --
--------------------------------------
addhook("say","sample.sayfuncs.say")
function sample.sayfuncs.say(p,txt)

	-- Time e.g.: 04:36 pm
	if (txt=="time") then msg(os.date("Time: %I:%M %p")); end

	-- Date e.g.: Wednesday, 25 Feb 2009
	if (txt=="date") then msg(os.date("Date: %A, %d %b %Y")); end

	-- Slap me
	if (txt=="slap me") then parse("slap "..p); end

	-- Show idlers (idle for 10 secs or longer)
	if (txt=="idlers") then
		idlers=0
		for i=1,32 do
			if (player(i,"exists")) then
				if (player(i,"idle")>9) then
					idlers=idlers+1
					msg(player(i,"name").." is idle for "..player(i,"idle").." secs!")
				end
			end
		end
		if (idlers==0) then msg("Nobody idles!") end
	end
	
	-- SOUND: That's the way I like it
	if (txt=="the way") then parse("sv_sound \"fun/thats_the_way.wav\""); end

	-- SOUND: Okay let's go
	if (txt=="lets go") then parse("sv_sound \"hostage/hos2.wav\""); end

	-- SOUND: Buaraarahahrahrrrr harr!
	if (txt=="zombies!") then parse("sv_sound \"player/zm_spray.wav\""); end

	-- SOUND: Mystery
	if (txt=="mystery") then parse("sv_sound \"env/mystery.wav\""); end
	
end

