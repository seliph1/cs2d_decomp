--------------------------------------------------
-- HUD Text Script by Unreal Software           --
-- 14.03.2011 - www.UnrealSoftware.de           --
-- Adds a custom text to the HUD                --
-- mp_hudscale should be 0 for this script!     --
--------------------------------------------------

if sample==nil then sample={} end
sample.hudtxt={}
sample.hudtxt.timer=0

-----------------------
-- SECOND            --
-----------------------
addhook("second","sample.hudtxt.second")
function sample.hudtxt.second()
	sample.hudtxt.timer=sample.hudtxt.timer+1
	if sample.hudtxt.timer>=5 then
		sample.hudtxt.timer=0
		for i=1,32,1 do
			if player(i,"exists") then
			
				local x=320 --player(i,"screenw")/2
				local y=240 --player(i,"screenh")/2
			
				-- HUD Color Fade Sample ( see http://www.cs2d.com/help.php?cat=server&cmd=hudtxtcolorfade#cmd )
				parse('hudtxt2 '..i..' 0 "©255255255This is just a HUD Text sample! The text will fade to black!" '..x..' '..y..' 1')
				parse('hudtxtcolorfade '..i..'  0 5000 0 0 0')
				y=y+20
				
				parse('hudtxt2 '..i..' 1 "...and this text will become red!" '..x..' '..y..' 1')
				parse('hudtxtcolorfade '..i..'  1 5000 255 0 0')
				y=y+20
				
				-- HUD Alpha Fade Sample ( see http://www.cs2d.com/help.php?cat=server&cmd=hudtxtalphafade#cmd )
				parse('hudtxt2 '..i..' 2 "this one will become invisible!" '..x..' '..y..' 1')
				parse('hudtxtalphafade '..i..'  2 5000 0.0')
				y=y+20
				
				-- HUD Move Sample ( see http://www.cs2d.com/help.php?cat=server&cmd=hudtxtmove#cmd )
				parse('hudtxt2 '..i..' 3 "<-- go left" '..x..' '..y..' 1')
				parse('hudtxtmove '..i..'  3 5000 '..(x-100)..' '..y..'')
				y=y+20
				
				parse('hudtxt2 '..i..' 4 "go right -->" '..x..' '..y..' 1')
				parse('hudtxtmove '..i..'  4 5000 '..(x+100)..' '..y..'')
				y=y+20
				
				parse('hudtxt2 '..i..' 5 "go down" '..x..' '..y..' 1')
				parse('hudtxtmove '..i..'  5 5000 '..x..' '..(y+100)..'')
				
			end
		end
	end
end