--------------------------------------------------
-- HUD Image Script by Unreal Software          --
-- 17.04.2017 - www.UnrealSoftware.de           --
-- Adds custom HUD images                       --
-- Adjusts to mp_hudscale (on round start only) --
--------------------------------------------------

if sample==nil then sample={} end
sample.hudimg={}
sample.hudimg.path="gfx/hud_buildhelper.png"

-----------------------
-- JOIN              --
-----------------------
addhook("join","sample.hudimg.join")
function sample.hudimg.join(id)
	sample.hudimg.createForPlayer(id)
end

-----------------------
-- STARTROUND        --
-----------------------
addhook("startround","sample.hudimg.startround")
function sample.hudimg.startround()
	local i
	for i=1,32 do
		if (player(i,"exists")) then
			sample.hudimg.createForPlayer(i)
		end
	end
end

-----------------------
-- Create Images     --
-----------------------
function sample.hudimg.createForPlayer(id)
	-- Adjust the screen size used for rendering depending on mp_hudscale
	-- See http://www.cs2d.com/help.php?cat=all&cmd=mp_hudscale#cmd
	local hudScaleMode = tonumber(game("mp_hudscale"))
	local scrWidth
	local scrHeight
	
	if (hudScaleMode==1) then
		scrWidth=850
		scrHeight=480
	elseif (hudScaleMode==2 or hudScaleMode==3) then
		scrWidth=640
		scrHeight=480
	else
		scrWidth=player(id,"screenw")
		scrHeight=player(id,"screenh")
	end
	
	-- This is just for debugging, you can comment it out if you want to :)
	print("creating images for player "..id..", scale mode: "..hudScaleMode..", ref res:"..scrWidth.."x"..scrHeight)
	
	-- The images are added as HUD images (mode 2)
	-- See http://www.cs2d.com/help.php?luacat=image&luacmd=image#cmd
	
	-- Top Left Corner (red)
	local iid=image(sample.hudimg.path, 0, 0, 2, id)
	imageblend(iid,1)
	imagecolor(iid,255,0,0)
	
	-- Top Right Corner (green)
	iid=image(sample.hudimg.path, scrWidth, 0, 2, id)
	imageblend(iid,1)
	imagecolor(iid,0,255,0)
	
	-- Bottom Left Corner (blue)
	iid=image(sample.hudimg.path, 0, scrHeight, 2, id)
	imageblend(iid,1)
	imagecolor(iid,0,0,255)
	
	-- Bottom Right Corner (yellow)
	iid=image(sample.hudimg.path, scrWidth, scrHeight, 2, id)
	imageblend(iid,1)
	imagecolor(iid,255,255,0)
	
	-- Center (magenta)
	iid=image(sample.hudimg.path, scrWidth / 2, scrHeight / 2, 2, id)
	imageblend(iid,1)
	imagecolor(iid,255,0,255)	
end