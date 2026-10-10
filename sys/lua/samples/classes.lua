--------------------------------------------------
-- Player Classes Script by Unreal Software     --
-- 28.02.2009 - www.UnrealSoftware.de           --
-- Adds Player Classes to your server           --
--------------------------------------------------

if sample==nil then sample={} end
sample.classes={}

-----------------------
-- INITIAL SETUP     --
-----------------------
function initArray(m)
	local array = {}
	for i = 1, m do
		array[i]=0
	end
	return array
end
sample.classes.class=initArray(32)
function sample.classes.classmenu(id)
	menu(id,"Select your Class,Soldier|Armor+MG,Spy|Stealth,(Engineer|Wrench),Pyro|Flamethrower,Scout|Machete,Sniper|AWP")	
end


-----------------------
-- TEAM -> CLASS     --
-----------------------
addhook("team","sample.classes.team")
function sample.classes.team(id,team)
	if (team>0) then
		sample.classes.classmenu(id)
	end
end


-----------------------
-- SERVERACTION      --
-----------------------
addhook("serveraction","sample.classes.serveraction")
function sample.classes.serveraction(id)
	sample.classes.classmenu(id)
end


-----------------------
-- CLASS SELECTION   --
-----------------------
addhook("menu","sample.classes.menu")
function sample.classes.menu(id,menu,sel)
	if (menu=="Select your Class") then
		if (sel>=0 and sel<=6) then
			sample.classes.class[id]=sel
			if (player(id,"health")>0) then
				parse("killplayer "..id)
			end
		end
	end
end


-----------------------
-- SPAWN             --
-----------------------
addhook("spawn","sample.classes.spawn")
function sample.classes.spawn(id)
	-- SOLDIER
	if (sample.classes.class[id]<=1) then
		parse ("setmaxhealth "..id.." 150")
		parse ("setarmor "..id.." 202")
		parse ("speedmod "..id.." -5")
		return "40,4,51";
	end
	-- SPY
	if (sample.classes.class[id]==2) then
		parse ("setmaxhealth "..id.." 100")
		parse ("setarmor "..id.." 206")
		parse ("speedmod "..id.." 5")
		return "21,1";
	end
	-- ENGINEER
	if (sample.classes.class[id]==3) then
		parse ("setmaxhealth "..id.." 100")
		parse ("setarmor "..id.." 50")
		return "10,2,74";
	end
	-- PYRO
	if (sample.classes.class[id]==4) then
		parse ("setmaxhealth "..id.." 125")
		parse ("setarmor "..id.." 75")
		return "46,6,73";
	end
	-- SCOUT
	if (sample.classes.class[id]==5) then
		parse ("setmaxhealth "..id.." 75")
		parse ("setarmor "..id.." 0")
		parse ("speedmod "..id.." 15")
		return "5,69,54";
	end
	-- SNIPER
	if (sample.classes.class[id]==6) then
		parse ("setmaxhealth "..id.." 75")
		parse ("setarmor "..id.." 25")
		return "35,3,53";
	end
end


-----------------------
-- NO BUYING         --
-----------------------
addhook("buy","sample.classes.buy")
function sample.classes.buy()
	return 1
end


-----------------------
-- NO COLLECTING     --
-----------------------
addhook("walkover","sample.classes.walkover")
function sample.classes.walkover(id,iid,type)
	if (type>=61 and type<=68) then
		return 0
	end
	return 1
end


-----------------------
-- NO DROPPING       --
-----------------------
addhook("drop","sample.classes.drop")
function sample.classes.drop()
	return 1
end


-----------------------
-- NO DEAD DROPPING  --
-----------------------
addhook("die","sample.classes.die")
function sample.classes.die()
	return 1
end