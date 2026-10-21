--------------------------------------------------
-- Tile Mapper Script by Unreal Software        --
-- 05.04.2014 - www.UnrealSoftware.de           --
-- Edit and inspect tiles                       --
--------------------------------------------------

if sample==nil then sample={} end
sample.tm={}
sample.tm.frame={}
sample.tm.x={}
sample.tm.y={}
sample.tm.mode={}
for i=1,32 do
	sample.tm.frame[i]=0
	sample.tm.x[i]=0
	sample.tm.y[i]=0
	sample.tm.mode[i]=0
end

-----------------------
-- SPAWN             --
-----------------------
addhook("spawn","sample.tm.spawn")
function sample.tm.spawn(id)
	parse('hudtxt2 '..id..' 0 "[F2] Set tile [F3] Get tile [F4] Tile info" 5 105 0')
end


-----------------------
-- SERVERACTION      --
-----------------------
addhook("serveraction","sample.tm.serveraction")
function sample.tm.serveraction(id,key)
	if (player(id,"health")>0 and player(id,"team")>0) then
		if (key==1) then
			-- set tile
			sample.tm.mode[id]=0
			reqcld(id,2)
		elseif (key==2) then
			-- get tile
			sample.tm.mode[id]=1
			reqcld(id,2)
		elseif (key==3) then
			-- tile info
			sample.tm.mode[id]=2
			reqcld(id,2)
		end
	end
end


-----------------------
-- Incoming Data     --
-----------------------
addhook("clientdata","sample.tm.clientdata")
function sample.tm.clientdata(id,mode,x,y)
	-- Change tile if incoming data is absolute cursor position (2)
	if (mode==2) then
		tilex=math.floor(x/32)
		tiley=math.floor(y/32)
		if (sample.tm.mode[id]==0) then
			-- set tile
			parse('settile '..tilex..' '..tiley..' '..sample.tm.frame[id])
			parse('sv_msg2 '..id..' "changed tile at '..tilex..'|'..tiley..'"')
		elseif (sample.tm.mode[id]==1) then
			-- get tile
			tmpframe=tile(tilex,tiley,"frame")
			if tmpframe>=0 then
				sample.tm.frame[id]=tmpframe
				parse('sv_msg2 '..id..' "tile frame is now '..tmpframe..'"')
			end
		elseif (sample.tm.mode[id]==2) then
			-- tile info
			if tile(tilex,tiley,"hascustomframe") then
				customframe='true'
			else
				customframe='false'
			end
			txtmsg=tilex..'|'..tiley..' frame: '..tile(tilex,tiley,"frame")..' prop: '..tile(tilex,tiley,"property")..' custom: '..customframe..' original: '..tile(tilex,tiley,"originalframe")
			parse('hudtxt2 '..id..' 1 "'..txtmsg..'" 5 125 0')
		end
	end
end