--------------------------------------------------
-- Spawn Equip by Unreal Software               --
-- 22.02.2011 - www.UnrealSoftware.de           --
-- Equip players with certain items on spawn    --
--------------------------------------------------

if sample==nil then sample={} end
sample.spawnequip={}

-- Equip the following items (10 = M3 Shotgun, 51 = HE Grenade)
sample.spawnequip.itemlist="10,51";

--------------------------------------
-- SPAWN                            --
--------------------------------------
addhook("spawn","sample.spawnequip.spawn")
function sample.spawnequip.spawn()
	return sample.spawnequip.itemlist;
end