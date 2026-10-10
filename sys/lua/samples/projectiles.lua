--------------------------------------------------
-- Projectiles Script by Unreal Software        --
-- 05.02.2013 - www.UnrealSoftware.de           --
-- Spawn 8 grenades at once when hitting F2     --
-- (and pistols or rockets on F3/F4)            --
--------------------------------------------------

if sample==nil then sample={} end
sample.projectiles={}

-----------------------
-- SERVERACTION      --
-----------------------
addhook("serveraction","sample.projectiles.serveraction")
function sample.projectiles.serveraction(id,key)
	if (player(id,"health")>0 and player(id,"team")>0) then
		if (key==1) then
			for i=0,315,45 do
				parse("spawnprojectile "..id.." 51 "..player(id,"x").." "..player(id,"y").." 200 "..i)
			end
		elseif (key==2) then
			for i=0,315,45 do
				parse("spawnprojectile "..id.." "..math.random(1,6).." "..player(id,"x").." "..player(id,"y").." 200 "..i)
			end
		elseif (key==3) then
			for i=0,315,45 do
				parse("spawnprojectile "..id.." 47 "..player(id,"x").." "..player(id,"y").." 500 "..i)
			end
		end
	end
end
