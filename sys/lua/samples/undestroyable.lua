--------------------------------------------------
-- Undestroyable Script by Unreal Software      --
-- 05.08.2010 - www.UnrealSoftware.de           --
-- Makes all dynamic objects undestroyable      --
--------------------------------------------------

if sample==nil then sample={} end
sample.undestroyable={}

--------------------------------------
-- UNDESTROYABLE                    --
--------------------------------------
addhook("objectdamage","sample.undestroyable.damage")
function sample.undestroyable.damage()
	return 1
end