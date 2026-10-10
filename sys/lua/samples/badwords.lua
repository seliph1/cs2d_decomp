--------------------------------------------------
-- Simple Badwords Filter by Unreal Software    --
-- 22.02.2009 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.badwords={}

--------------------------------------
-- BADWORDS TABLE (LOWERCASE)       --
--------------------------------------
sample.badwords.words = {"hitler","fuck","bitch","cunt","ass","whore","nigger","shit"}


--------------------------------------
-- HANDLE BADWORDS IN SAY MESSAGES  --
--------------------------------------
addhook("say","sample.badwords.say")
function sample.badwords.say(p,t)
	t=string.lower(t)
	for i = 1,#sample.badwords.words do
		if (string.find(t,sample.badwords.words[i])~=nil) then
			msg(player(p,"name").." said a bad word!")
			parse("kick "..p)
			break
		end
	end
end