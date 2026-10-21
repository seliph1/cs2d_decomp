--------------------------------------------------
-- Tween Script by Unreal Software              --
-- 12.03.2011 - www.UnrealSoftware.de           --
-- Adds an image which follows player 1         --
--------------------------------------------------

if sample==nil then sample={} end
sample.tween={}
sample.tween.image=0

-----------------------
-- INITIAL SETUP     --
-----------------------
function sample.tween.setup()
	sample.tween.image=image("gfx/sprites/flare2.bmp",0,0,3)	-- Create image @ Player
	imagecolor(sample.tween.image,255,255,0)					-- Make image yellow
	imageblend(sample.tween.image,1)							-- Make image glow
	tween_rotateconstantly(sample.tween.image,5)
end
sample.tween.setup()

-----------------------
-- SECOND            --
-----------------------
addhook("second","sample.tween.second")
function sample.tween.second()
	if player(1,"exists") then
		if player(1,"health")>0 then
			tween_move(sample.tween.image,1000,player(1,"x"),player(1,"y"))
			tween_color(sample.tween.image,1000,math.random(0,255),math.random(0,255),math.random(0,255))
			tween_scale(sample.tween.image,1000,math.random(1,3)/2.0,math.random(1,3)/2.0)
		end
	end
end

-----------------------
-- STARTROUND        --
-----------------------
addhook("startround","sample.tween.startround")
function sample.tween.startround()
	sample.tween.setup()
end