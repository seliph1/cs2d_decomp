--------------------------------------------------
-- Fade Out Explode Script by Unreal Software   --
-- 04.09.2019 - www.UnrealSoftware.de           --
-- Let's item explode when they fade out        --
--------------------------------------------------

if sample==nil then sample={} end
sample.fadeoutexplode={}
sample.fadeoutexplode.explosionSize = 64
sample.fadeoutexplode.explosionDamage = 50

-----------------------
-- ITEMFADEOUT       --
-----------------------
addhook("itemfadeout","sample.fadeoutexplode.itemfadeout")
function sample.fadeoutexplode.itemfadeout(iid, t, tileX, tileY)
	local x = tileX * 32 + 16
	local y = tileY * 32 + 16
	local size = sample.fadeoutexplode.explosionSize
	local damage = sample.fadeoutexplode.explosionDamage
	local srcPlayer = 0
	parse("explosion "..x.." "..y.." "..size.." "..damage.." "..srcPlayer)
end