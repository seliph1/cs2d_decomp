--------------------------------------------------
-- Registered Only Script by Unreal Software    --
-- 23.02.2009 - www.UnrealSoftware.de           --
-- Only USGN users are allowed to join a team   --
--------------------------------------------------

if sample==nil then sample={} end
sample.regonly={}

-----------------------
-- Team Hook         --
-----------------------
addhook("team","sample.regonly.team")
function sample.regonly.team(id,team)
	if (team>0) then
		-- USGN only!
		if (player(id,"usgn")>0) then
			-- Allow USGN users
			return 0
		else
			-- Don't allow unregistered
			msg2(id,"©255000000Only USGN users are allowed to join a team on this server!@C")
			msg2(id,"©255000000Please login or register!@C")
			return 1
		end
	else
		-- Everyone is allowed to be spectator!
		return 0
	end
end

-- NOTE THE FOLLOWING:
-- team 0 = spectators
-- team 1 = terrorists
-- team 2 = counter-terrorists (team 3 = counter-terrorists, VIP for as_ maps)