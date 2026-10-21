--------------------------------------------------
-- Simple Advertising by Unreal Software        --
-- 22.02.2009 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.ads={}

--------------------------------------
-- WELCOME MESSAGE ON JOIN          --
--------------------------------------
addhook("join","sample.ads.join")
function sample.ads.join(p)
	msg2(p,"Welcome on my Server, "..player(p,"name").."!")
end


--------------------------------------
-- ADVERTISING EVERY MINUTE         --
--------------------------------------
addhook("minute","sample.ads.minute")
function sample.ads.minute()
	msg("This server is powered by")
	msg("www.UnrealSoftware.de & www.CS2D.com")
end