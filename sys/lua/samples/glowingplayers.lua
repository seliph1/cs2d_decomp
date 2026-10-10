--------------------------------------------------
-- Glowing Players Script by Unreal Software    --
-- 08.11.2009 - www.UnrealSoftware.de           --
--------------------------------------------------

if sample==nil then sample={} end
sample.glowing={}

--------------------------------------
-- GLOW                             --
--------------------------------------

-- Glow Function
function sample.glowing.makeallglow()
	for i=1,32,1 do
		id=image("gfx/sprites/flare2.bmp",0,0,100+i)	-- Create image @ Player
		imagecolor(id,255,255,0)						-- Make image yellow
		imageblend(id,1)								-- Make image glow
		imagealpha(id,0.5)								-- Decrease Glow Strength
	end
end

-- Make Glow instantly after starting server
sample.glowing.makeallglow()

-- Make Glow after roundstart (because images are deleted on roundstart!)
addhook("startround","sample.glowing.startround")
function sample.glowing.startround()
	sample.glowing.makeallglow()
end