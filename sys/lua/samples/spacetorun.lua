--------------------------------------------------
-- Space to Run Script by Unreal Software       --
-- 01.12.2017 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.run={}

--------------------------------------
-- HIT SPACE TO RUN                 --
--------------------------------------
addbind("space")
addhook("key","sample.run.bind")
addhook("spawn","sample.run.spawn")

function sample.run.bind(id, key, state)
	if (key == "space") then
		if (state == 1) then
			parse("speedmod "..id.." 15")
			parse("shake "..id.." 5")
		end
		if (state == 0) then
			parse("speedmod "..id.." 0")
			parse("shake "..id.." 5")
		end
	end
end

function sample.run.spawn(id)
	msg2(id,"Press [Space] to run!@C")
end