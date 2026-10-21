--------------------------------------------------
-- Image Hover Script by Unreal Software        --
-- 21.04.2017 - www.UnrealSoftware.de           --
-- Draws an image which blinks on mouseover     --
--------------------------------------------------

if sample==nil then sample={} end
sample.imghover={}
sample.imghover.path="gfx/hud_slot.bmp"
sample.imghover.x=545
sample.imghover.y=415
sample.imghover.w=150
sample.imghover.h=40
sample.imghover.imageids={}
sample.imghover.updaterate=150													-- Update rate in ms (fast updates may lead to lags!)
timer(sample.imghover.updaterate,"sample.imghover.requestdata","",0)			-- Timer to request updates

-----------------------
-- JOIN              --
-----------------------
addhook("join","sample.imghover.join")
function sample.imghover.join(id)
	sample.imghover.createForPlayer(id)
end

-----------------------
-- STARTROUND        --
-----------------------
addhook("startround","sample.imghover.startround")
function sample.imghover.startround()
	local i
	for i=1,32 do
		if (player(i,"exists")) then
			sample.imghover.createForPlayer(i)
		else
			sample.imghover.imageids[id]=nil
		end
	end
end

-----------------------
-- Create Images     --
-----------------------
function sample.imghover.createForPlayer(id)
	-- Create Image 
	local imageId=image(sample.imghover.path, sample.imghover.x, sample.imghover.y, 2, id)
	imageblend(imageId,1)
	imagecolor(imageId,255,255,0)
	sample.imghover.imageids[id]=imageId
end

-----------------------
-- Request Data      --
-----------------------
function sample.imghover.requestdata()
	-- Request cursor positions (0) off ALL players (0)
	reqcld(0,0) -- (reqcl = request client data)
end

-----------------------
-- Incoming Data     --
-----------------------
addhook("clientdata","sample.imghover.clientdata")
function sample.imghover.clientdata(id,mode,x,y)
	-- Check if image is hovered. Change color depending on result
	if (mode==0) then
		if (player(id,"bot")==false and sample.imghover.imageids[id] ~= nil) then
			print("checking if mouse of player "..id.." is over image, mouse pos: "..x.."|"..y)
		
			-- Lua images are centered, take this into account by adding half the size from the mouse position for the check!
			x = x + (sample.imghover.w / 2)
			y = y + (sample.imghover.h / 2)
		
		
			if (x >= sample.imghover.x and y >= sample.imghover.y and x < (sample.imghover.x+sample.imghover.w) and y < (sample.imghover.y+sample.imghover.h)) then
				-- Hovered! -> make red
				imagecolor(sample.imghover.imageids[id],255,0,0)
			else
				-- Not hovered! -> make yellow
				imagecolor(sample.imghover.imageids[id],255,255,0)
			end
		end
	end
end