--------------------------------------------------
-- Fast Players Script by Unreal Software       --
-- 03.03.2009 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.fast={}

--------------------------------------
-- MAKE FAST                        --
--------------------------------------
addhook("spawn","sample.fast.spawn")
function sample.fast.spawn(p)
	parse("speedmod "..p.." 15")
end


--[[
	speedmod accepts values from
	-100 (no movement)
	over
	0 (regular movement)
	to
	100 (very fast movement)
]]--