--------------------------------------------------
-- Hitzone Script by Unreal Software            --
-- 10.02.2013 - www.UnrealSoftware.de           --
-- Adds a moving image with hitzone             --
--------------------------------------------------

if sample==nil then sample={} end
sample.hitzone={}
sample.hitzone.id=0

-----------------------
-- STARTROUND        --
-----------------------
addhook("startround","sample.hitzone.startround")
function sample.hitzone.startround()
	sample.hitzone.id=0
end

-----------------------
-- SERVERACTION      --
-----------------------
addhook("serveraction","sample.hitzone.serveraction")
function sample.hitzone.serveraction(id,key)
	-- Create Image
	if sample.hitzone.id==0 then
		sample.hitzone.id=image("gfx/cs2d.bmp",player(1,"x"),player(1,"y"),0)
	else
		imagepos(sample.hitzone.id,player(1,"x"),player(1,"y"),0)
	end
	-- Move
	tween_move(sample.hitzone.id,5000,player(1,"x")+300,player(1,"y")+300)
	-- Add Hitzone (Mode 104: Green blood + block shot)
	imagehitzone(sample.hitzone.id,104,-92/2,-92/2,92,92)
end

-----------------------
-- HITZONE           --
-----------------------
addhook("hitzone","sample.hitzone.hit")
function sample.hitzone.hit(id,player,object,weapon,x,y)
	-- Show Message
	msg("Hit "..id.." (pl:"..player.." obj:"..object.." wpn:"..weapon.." @ "..x..","..y..")")
	-- Fire on Hit
	parse('effect "fire" '..x.." "..y.." 5 3")
end