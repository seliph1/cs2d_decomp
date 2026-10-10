--------------------------------------------------
-- Console Script by Unreal Software            --
-- 23.02.2009 - www.UnrealSoftware.de           --
-- Adds new console/script commands             --
-- Works only when running a single command!    --
--------------------------------------------------

if sample==nil then sample={} end
sample.console={}

-----------------------
-- Parser Hook       --
-----------------------
addhook("parse","sample.console.parse")
function sample.console.parse(cmd)

	-- My Server Info
	if (cmd=="myserverinfo") then
		print("Server Name: "..game("sv_name"))
		print("Max Players: "..game("sv_maxplayers"))
		return 2
	end

	-- Health List
	if (cmd=="healthlist") then
		for i=1,32 do
			if (player(i,"exists")) then
				hp=player(i,"health")
				if (hp>90) then
					print("©000255000"..player(i,"name").." -> HP: "..hp)
				elseif (hp>60) then
					print("©255255000"..player(i,"name").." -> HP: "..hp)
				elseif (hp>30) then
					print("©255128000"..player(i,"name").." -> HP: "..hp)
				else
					print("©255000000"..player(i,"name").." -> HP: "..hp)
				end

			end
		end
		return 2
	end
	
	-- Encage Players
	if (cmd=="encage") then
		parse("sv_msg ©255000000ENCAGING ALL PLAYERS!!!@C")
		a=player(0,"table")
		for i=1,#a do
			if (player(a[i],"team")>0) then
				if (player(a[i],"health")>0) then
					x=player(a[i],"tilex")
					y=player(a[i],"tiley")
					parse ("spawnobject 2 "..(x-1).." "..(y-1))
					parse ("spawnobject 2 "..(x).." "..(y-1))
					parse ("spawnobject 2 "..(x+1).." "..(y-1))
					parse ("spawnobject 2 "..(x-1).." "..(y))
					parse ("spawnobject 2 "..(x+1).." "..(y))
					parse ("spawnobject 2 "..(x-1).." "..(y+1))
					parse ("spawnobject 2 "..(x).." "..(y+1))
					parse ("spawnobject 2 "..(x+1).." "..(y+1))
				end
			end
		end
		return 2
	end
	
	-- Get Entity List
	if (cmd=="getentitylist") then
		local list=entitylist()
		for _,e in pairs(list) do
			print("entity @ ("..e.x..","..e.y..") - "..entity(e.x,e.y,"typename"))
		end
		return 2
	end
	
	-- Get Projectile List
	if (cmd=="getprojectilelist") then
		print("flying projectiles:")
		local plist=projectilelist(0,0)
		for _,p in pairs(plist) do
			print("id "..p.id.." of player "..p.player.." @ ("..projectile(p.id,p.player,"x")..","..projectile(p.id,p.player,"y")..")")
		end
		print("ground projectiles:")
		plist=projectilelist(1,0)
		for _,p in pairs(plist) do
			print("id "..p.id.." of player "..p.player.." @ ("..projectile(p.id,p.player,"x")..","..projectile(p.id,p.player,"y")..")")
		end
		return 2
	end

	-- No Command found, normal parsing
	return 0
end