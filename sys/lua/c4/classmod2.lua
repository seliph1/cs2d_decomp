print("\169115110255Lua: Classmod 2")


function init(m)
	local array = {}
	for i = 1, m do
		array[i]=0
	end
	return array
end

local CM_SPRITE_PATH = "gfx/c4/projectile.png"
local CM_SPRITESHEET_PATH = "<spritesheet:gfx/c4/projectile.png:32:32:n>"
local CM_SPRITE64 = "gfx/c4/projectile64.png"
local CM_SPRITESHEET64 = "<spritesheet:gfx/c4/projectile64.png:128:128:n>"
local CM_SPRITESHEET256 = "<spritesheet:gfx/c4/projectile64.png:256:256:n>"


local CM_RAINBOW = {
	{255,0,0},
	{234,104,0},
	{232,225,0},
	{0,234,0},
	{0,189,234},
	{1,3,234},
	{90,2,234},
}
local CM_LASER_SFX = ""
local CM_KNIFE_SFX = ""

local CM_NO_COOLDOWN = false

cm = {}

cm.hud_position_x=425
cm.hud_position_y=420

cm.player 					= init(32)
cm.playercd 				= init(32)
cm.player_select 			= init(32)
cm.player_spawnprotection 	= init(32)
cm.playerskin				= init(32)
cm.ticket					= init(32)

cm.damage_indicator 		= init(32)
cm.damage_timer 			= init(32)
cm.attackdelay 				= init(32)

cm.player_cooldown			= {}
cm.player_input				= {}
cm.player_input_timer		= {}
cm.clone					= {}
cm.player_trace 			= {}
cm.player_imgslot 			= {}
cm.player_timers 			= {}
cm.player_extra 			= {}
cm.player_dieposition 		= {}
cm.player_lastposition 		= {}

cm.winrate 					= {}


for i=1,32 do
	cm.player_input[i]			= {}
	cm.player_input_timer[i]	= {}
	cm.player_trace[i] 			= {}
	cm.player_imgslot[i] 		= {}
	cm.player_timers[i] 		= {}
	cm.player_extra[i] 			= {}
	cm.player_dieposition[i] 	= {
		[1] = 0,
		[2] = 0,
	}
	
	cm.player_lastposition[i] 	= {
		[1] = 0,
		[2] = 0,
	}
	cm.player_cooldown[i]		= {
		["Q"]			= 0,
		["E"]			= 0,
		["space"]		= 0,
		["mouse2"]		= 0,
		["mouse1"]		= 0,
		["leftshift"]	= 0,
	}
	
	cm.clone[i] = {
		x			= 0,
		y			= 0,
		rot			= 0,
		img			= 0,
		weapon_img	= 0,
	}
	
end

cm.clone_ref 		= {}
cm.image_projectile = {}
cm.image_npc 		= {}

cm.main_clock=0
--cm.side_clock=0

cm.building_allowed={
	[1]=6; --"Barricade"
	[2]=6; -- Barbed Wire"
	[3]=6; --"Wall I"
	[4]=4; --"Wall II"
	[5]=2; --"Wall III"
	[6]=6; --"Gate Field"
	[7]=2; --"Dispenser"
	[8]=4; --"Turret"
	[9]=1; --"Supply"
	[13]=0; --"Teleporter Entrance"
	[14]=0; --"Teleporter Exit"
}

-----------------------------------------------------------------------------------------------

cm.classdata = {
	[0]={name="Random",equips="X",desc="Nenhum"};
	[1]={
		name="Arrow",
		category="DPS",
		equips="X",
		--melee="hand",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.arrow(uid,1,0)
			return 1
		end,
		
		abilitydesc="Throw 5 arrows in a volley.",
		onability=function(uid)
			cm.arrow(uid,1,-15)
			cm.arrow(uid,1,-7)
			cm.arrow(uid,1,0)
			cm.arrow(uid,1,7)
			cm.arrow(uid,1,15)
			return 5
		end,
		
		ultimatedesc="Throw 3 straight arrows.",
		onultimate=function(uid)
			cm.arrow(uid,1,0)
			timerex(70,cm.arrow,1,uid,1)
			timerex(140,cm.arrow,1,uid,1)
			timerex(210,cm.arrow,1,uid,1)
			return 10
		end,
	},
	
	[2]={
		name="Ice Arrow",
		category="DPS/Support",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.icearrow(uid,24,0)
			return 1
		end,
		
		abilitydesc="Throw 3 ice arrows in a volley",
		onability=function(uid)
			cm.icearrow(uid,24,-7)
			cm.icearrow(uid,24,0)
			cm.icearrow(uid,24,7)
			return 10
		end,
		
		ultimatedesc="Throw a frost arrow.",
		onultimate=function(uid)
			cm.frostarrow(uid,15,0)
			return 8
		end,
			
	},
	[3]={
		name="Fire Arrow",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.firearrow(uid,14,0)
			return 1
		end,
		ultimatedesc="Throw a burning arrow.",
		onultimate=function(uid)
			cm.burnarrow(uid,14,0)
			return 10
		end,
		abilitydesc="Throw a piercing arrow.",
		onability=function(uid)
			cm.ichorcharge(uid)
			return 5.5
		end,
	},
	
	[4]={
		name="Gladius",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.gladius(uid)
			return 1
		end,
		
		abilitydesc="Teleport to the position of sword (Hold E)",
		onability=function(uid)
			cm.gladiustp(uid)
			return 5
		end,
		ultimatedesc="Spawn 8 swords that goes into the target.",
		onultimate=function(uid)
			--cm.smokearrow(uid,14,0)
			cm.unlimitedblade(uid)
			return 8
		end,
	},
	
	[5]={
		name="Tornado",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=40,
		onautoattack=function(uid)
			cm.windblow(uid)
		end,
		abilitydesc="Creates a tornado in target.",
		onability=function(uid)
			cm.windsurge(uid)
			return 10
		end,
		
		ultimatedesc="Stay immobile while a tornado spawns at your position.",
		onultimate=function(uid)
			--cm.smokearrow(uid,14,0)
			cm.tornado(uid)
			
			-- Stops player and restores speed after.
			local speed = cm.classdata[cm.player[uid]].speed or 0
			cm.speedmod(uid, speed - 100)
			timerex(1000,cm.speedmod, 1, uid, speed)
			return 3
		end,
	},
	
	[6]={
		name="Light",
		category="DPS/Support",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=40,
		onautoattack=function(uid)
			cm.lightbolt(uid)
		end,

		abilitydesc="Bind target.",
		onability=function(uid)
			cm.lightbind(uid)
			return 4
		end,
		ultimatedesc="Damage to targets in a straight line",
		onultimate=function(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			cm.lightbeam(uid,15)
			local victim = cm.linecolision(x,y,rot,1,15)
			local damage = math.random(50,80)
			for key,vid in pairs(victim) do
				if vid~=uid and not cm.sameteam(vid,uid) then
					cm.customdamage(uid,vid,damage,"Light Beam")
					cm.blink(vid,255,255,255)
				end	
			end
			return 10
		end,
	},
	
	[7]={
		name="Ice Shard",
		category="Support/Tank",
		equips="X",
		speed=0,
		health=200,
		melee="claw",
		
		onhit=function(vid,uid)
			if not cm.sameteam(vid,uid) then
				local x,y = cm.pos(vid)
				cm.effect("colorsmoke",x,y,3,3,190,255,255)
			end	
		end,
		
		abilitydesc="Throws an ice boulder that binds.",
		onability=function(uid)
			cm.iceboulder(uid)
			return 4
		end,
		
		ultimatedesc="Spawns 8 ice shards and does damage in area.",
		onultimate=function(uid)
			cm.icestorm(uid)
			local radius = 40
			for angle=0,360,radius do
				cm.iceshard(uid,angle)
			end
			return 8
		end,
	},
	
	
	[8]={
		name="Nebula",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=30,
		onautoattack=function(uid)
			cm.darkmatter(uid)
		end,
		
		abilitydesc="Guide a nebula with your mouse.",
		onability=function(uid)
			cm.nebula(uid)
			return 7
		end,
		ultimatedesc="Create a damaging black circle on the ground.",
		onultimate=function(uid)
			cm.eclipse(uid)
			return 8
		end,		
	},
	
	[9]={
		name="Fissure",
		category="Tank",
		equips="X",
		speed=-5,
		health=250,
		melee="claw",
		
		abilitydesc="Shake the ground.",
		onability=function(uid)
			cm.fissure(uid)
			
			return 0.6
		end,
		
		ultimatedesc="Throw a boulder.",
		onultimate=function(uid)
			cm.boulder(uid)
			return 5
		end,
	},
	
	[10]={
		name="Ninja",
		category="DPS",
		equips="X",
		speed=5,
		health=90,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.kunai(uid,85,0)
			cm.kunai(uid,85,-5)
			cm.kunai(uid,85,5)
		end,
		
		ultimatedesc="Teleport to the position of kunai",
		onultimate=function(uid)
			cm.dissonance(uid)
			return 6
		end,
		
		abilitydesc="Throw a guided shuriken.",
		onability=function(uid)
			cm.shuriken(uid)
			return 7
		end,
	},
	
	[11]={
		name="Bubble",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
	
		attackdelay=10,
		onautoattack=function(uid)
			local offset = math.random(0,5)-2.5
			local tiles = math.random(12,16)
			cm.bubble(uid,offset,tiles)
		end,
		
		ultimatedesc="Cast a high damage slow bubble",
		onultimate=function(uid)
			cm.bigbubble(uid)
			return 8
		end,
		abilitydesc="Throw a poisonous bubble",
		onability=function(uid)
			cm.sulphurbubble(uid)
			return 6
		end,
	},
	
	[12]={
		name="Bard",
		category="Support",
		equips="X",
		speed=0,
		health=100,
	
		attackdelay=40,
		onautoattack=function(uid)
			--local offset = math.random(0,30)-15
			cm.musicalnote(uid,20)
			cm.musicalnote(uid,0)
			cm.musicalnote(uid,-20)
		end,
		
		ultimatedesc="Create an healing aura around you.",
		onultimate=function(uid)
			cm.healingaura(uid)
			return 11
		end,
		
		abilitydesc="Cast an healing projectile.",
		onability=function(uid)
			cm.sonicnote(uid)
			return 8
		end,
	},
	[13]={
		name="Water",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
	
		attackdelay=25,
		onautoattack=function(uid)
			cm.whirlpool(uid)
		end,
		
		ultimatedesc="Summon a shark.",
		onultimate=function(uid)
			cm.shark(uid)
			return 6
		end,
		abilitydesc="Create a water vortex",
		onability=function(uid)
			cm.waterstream(uid)
			return 12
		end,
	},
	
	[14]={
		name="Thorn",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=15,
		onautoattack=function(uid)
			local offset = math.random()*9 - 4.5
			local tiles = 7 + math.floor(math.random()*3 )
			cm.petal(uid,offset,tiles)
		end,
		ultimatedesc="Throw a thorn flower (Hold E)",
		onultimate=function(uid)
			cm.thorn(uid)
			return 4
		end,
		abilitydesc="Throw a flower (Hold E)",
		onability=function(uid)
			cm.flower(uid)
			return 1.5
		end,
	},
	
	[15]={
		name="Poison Arrow",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			cm.poisonarrow(uid)
		end,
		
		abilitydesc="Create a poisonous area",
		onability=function(uid)
			cm.poisonpotion(uid)
			return 7
		end,
		ultimatedesc="Do damage on everyone poisoned around your position.",
		onultimate=function(uid)
			return cm.contaminate(uid)
			
		end,
	},
	
	[16]={
		name="Blood",
		category="DPS/Tank",
		equips="X",
		speed=1,
		melee="claw",
		health=150,

		
		onhit = function(vid,uid,weapon,damage)
			local x,y = cm.pos(uid)
			local damage = damage or 0
			cm.effect("flare",x,y,5,5,162,13,29)
			cm.sethealth(uid, player(uid,"health") + math.random(0,math.floor(damage/2)))
			cm.healindicator(uid,uid,math.floor(damage/2))
		end,		
		
		ultimatedesc="Throw a blood drop",
		onultimate=function(uid)
			cm.blood(uid)
			return 1
		end,
		
		abilitydesc="Charge an explosive blood attack  (Hold E)",
		onability=function(uid)
			cm.bloodwave(uid)
			return 5
		end,
	},
	
	
	[17]={
		name="Leaf",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=20,
		onautoattack=function(uid)
			local offset = math.random()*3 - 1.5
			local tiles = 10 + math.floor(math.random()*3 )
			cm.leaf(uid,tiles,offset)
		end,
		
		abilitydesc="Pull all leaves thrown.",
		onability=function(uid)
			return cm.leafpull(uid)
		end,
		
		ultimatedesc="Throw 5 leaves.",
		onultimate=function(uid)
			--local offset = math.random()*6 - 3
			--local tiles = 7 + math.floor(math.random()*3 )
			cm.leaf(uid,10,14)			
			cm.leaf(uid,10,7)			
			cm.leaf(uid,10,0)
			cm.leaf(uid,10,-7)			
			cm.leaf(uid,10,-14)			
			return 7
		end
		
	},
	
	[18]={
		name="Anchor",
		category="Tank",
		equips="X",
		melee="claw",
		speed=0,
		health=250,
		
		abilitydesc="Throw an anchor, charge into the target.",
		onability=function(uid)
			cm.anchor(uid)
			return 6
		end,
		
		ultimatedesc="Throw a hook, bring the target to your position.",
		onultimate=function(uid)
			cm.hook(uid)
			return 6
		end
	},
	
	[19]={
		name="Blast",
		category="DPS",
		equips="X",
		speed=0,
		health=100,
		
		
		attackdelay=20,
		--[[
		onhit=function(vid,uid)
			if not cm.sameteam(vid,uid) and not cm.check_timed_effect(vid,"burn") then
				cm.burn(vid,uid)
			end	
		end,
		--]]
		
		onautoattack=function(uid)
			cm.blast(uid)
		end,
		abilitydesc="Throw an explosive meteor.",
		onability=function(uid)
			cm.meteor(uid)
			return 8
		end,
		--[[
		onultimate=function(uid)
			cm.flare(uid)
			return 0
		end
		--]]
		
	},
	
	[20]={
		name="Love Arrow",
		category="Support/DPS",
		equips="X",
		speed=0,
		health=100,
		
		attackdelay=30,
		onautoattack=function(uid)
			cm.lovearrow(uid)
		end,
		
		abilitydesc="Charge a piercing arrow (Hold E)",
		onability=function(uid)
			cm.piercinglove(uid)
			return 10
		end,
		
		ultimatedesc="Throw three arrows.",
		onultimate=function(uid)
			cm.lovearrow(uid,20)
			cm.lovearrow(uid,20,6)
			cm.lovearrow(uid,20,-6)
			return 3
		end,
		
	},
	
	-- Classmod
	[21]={
		name="Gunner";
		category="DPS";
		equips="1";
		weapon=1;
		speed=5,
		health=200,
		onattack=function(uid)
			if player(uid,"weapontype")==1 then
				cm.equip(uid,2)
				cm.setweapon(uid,2)
				cm.setammo(uid,2,10,1000)
				
				cm.strip(uid,1)
			elseif player(uid,"weapontype")==2 then
				cm.equip(uid,5)
				cm.setweapon(uid,5)
				cm.setammo(uid,5,10,1000)
				
				cm.strip(uid,2)
			elseif player(uid,"weapontype")==5 then
				cm.equip(uid,4)
				cm.setweapon(uid,4)
				cm.setammo(uid,4,10,1000)
				
				cm.strip(uid,5)
			elseif player(uid,"weapontype")==4 then
				cm.equip(uid,3)
				cm.setweapon(uid,3)
				cm.setammo(uid,3,10,1000)
				
				cm.strip(uid,4)
			elseif player(uid,"weapontype")==3 then
				cm.equip(uid,1)
				cm.setweapon(uid,1)
				cm.setammo(uid,1,10,1000)
				
				cm.strip(uid,3)
			end
		end;
		
		
		abilitydesc="Small dash in mouse direction",
		onability = function(uid)
			cm.afterimage(uid)
			return cm.dash(uid,5*32)
		end,
	};
	
	[22]={
		name="Medic";
		equips="82,4";
		category="Support/Tank";
		health=75;
		abilitydesc="Drop a medikit on the ground",
		onability=function(uid)
			local x,y = player(uid,"tilex"),player(uid,"tiley")
			cm.spawnitem("64",x,y)
			return 8
		end;
		
		onhit=function(vid,aid,weapon,hpdmg)
			if weapon==4 then
				local x,y = cm.pos(vid)
				if cm.sameteam(vid,aid) then
					cm.sethealth(vid,player(vid,"health")+20)
					cm.healindicator(vid,aid,20)
				else
					--cm.sethealth(aid,player(aid,"health") + math.floor(hpdmg/4))
					--cm.healindicator(aid,aid, math.floor(hpdmg/4) )
					
				end	
				cm.effect("flare",x,y,1,1,0,255,0)
			end	
		end;


	};
	
	[23]={
		name="Jason";
		equips="79";
		category="Tank";
		speed=-2;
		health=250;
		melee="chainsaw";
		skin="<spritesheet:gfx/player/hostages.bmp:32:32:b>";
		skinframe=6;
		skincolor={50,50,50};
		skinscale=1.1;
		abilitydesc="Gain a speed boost for 3 seconds.",
		onability=function(uid)
			local x,y=cm.pos(uid)
			cm.speedbuff(uid)
			return 10
		end;
	};
	[24]={
		name="Portal";
		category="Support";
		equips="88,5,77,87";
		health=150;
		melee="chainsaw";
		onkill = function(uid)
			local ammo,ammoin 
			ammo,ammoin = playerammo(uid,77)
			if not ammo then
				cm.equip(uid,87)
				cm.setammo(uid,87,1,0)
			else
				cm.setammo(uid,87,ammo+1,0)
			end
			
			ammo,ammoin = playerammo(uid,77)
			if not ammo then
				cm.equip(uid,77)
				cm.setammo(uid,77,1,0)
			else
				cm.setammo(uid,77,ammo+1,0)
			end

			cm.popup(uid,"+1 mine/laser mine",91)
		end,
		abilitydesc="Press E to teleport.",
		onability=function(uid)
			
			local mx,my = cm.mouse(uid)
			local tx,ty = cm.floor(mx,my)
			if tile(tx,ty,"walkable") then
				cm.afterimage(uid)
				cm.setpos(uid,mx,my)
				return 20
			else
				return -1,"Cannot teleport here"
			end
			
		end,
	};
	[25]={
		name="Piromaniac";
		category="DPS";
		equips="46";
		weapon=46;
		abilitydesc="Throw a molotov",
		onability=function(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			local mx,my = cm.mouse(uid)
			local dist = math.sqrt( (x - mx)^2 + (y - my)^2 )
			cm.spawnprojectile(uid,73,x,y,dist,rot)
			return 20
		end;
	};	
	[26]={
		name="Bin Laden";
		category="DPS";
		noknife=true,
		equips="47,4";
		weapon=47;
		speed=-5;
		onkill=function(uid,weapon,vid)
			local ammo,ammoin = playerammo(uid,47)
			cm.setammo(uid,47,ammoin,ammo+1)

			cm.popup(uid,"+1 ammo",91)
		end	
	};
	[27]={
		name="Bomberman";
		category="DPS";
		equips="51";
		weapon=51;
		inf=51;
		abilitydesc="Press and hold E to charge an explosion.",
		onability=function(uid)
			cm.explosioncharge(uid)
			return 8
		end;
	};
	
	[28]={
		name="Thief";
		category="Support";
		equips="84,1";
		candrop=true;
		canpick=true;
		health=115;
		speed=5;
		abilitydesc="Steal weapons from your enemies.",
		onability=function(uid)
			local x,y=cm.pos(uid)
			
			for index,vid in ipairs(closeplayers(x,y,64)) do 
				local vx,vy = cm.pos(vid)
				if not cm.sameteam(uid,vid) then 
				
					msg2(uid,"You stole "..player(vid,"name").."!") 
					msg2(vid,"You got robbed!") 
				
					for _,item in pairs(playerweapons(vid)) do
						if item ~= 78 then
							cm.strip(vid,item)
						end
						cm.equip(uid,item)
					end
					cm.effect("colorsmoke",x,y,20,20,0,0,0) 

					local h=math.random(50,100)
					cm.sethealth(uid,player(uid,"health")+h)
					cm.healindicator(uid,uid,h)
					return 16
				end
			end
			return -1,"No target!"
		end;
	};
	[29]={
		name="Shotgun";
		equips="10,11";
		category="DPS/Tank";
		health=120;
		weapon=10;
		desc="Recover health with damage dealt.";
		onhit=function(vid,aid,weapon,hpdmg)
			if not cm.sameteam(vid,aid) and (weapon==11 or weapon==12) then
				cm.sethealth(aid,player(aid,"health")+hpdmg)
				cm.healindicator(aid,aid,hpdmg)
				
				local x,y=cm.pos(aid) 
				cm.effect("flare",x,y,1,1,0,255,0) 
			end	
		end;
	};
	[30]={
		name="Soldier";
		category="DPS";
		equips="32,51,3";
		weapon=32;
		abilitydesc="Spawn a wall in front of you",
		onability=function(uid)
			local x,y,rot,mx,my = cm.angle(uid,32,0)
			local tx,ty = cm.floor(mx,my)
			if tile(tx,ty,"walkable") then
				cm.spawnobject(5,tx,ty,0,0,player(uid,"team"),uid)
				return 10
			else
				return -1,"Cannot spawn wall here."
			end
		end;
	};
	
	[31]={
		name="Terrorist";
		category="DPS";
		equips="ak-47";
		weapon=30;
		
		abilitydesc="Throw a HE Grenade",
		onability=function(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			local mx,my = cm.mouse(uid)
			local dist = math.sqrt( (x - mx)^2 + (y - my)^2 )
			cm.spawnprojectile(uid,51,x,y,dist,rot)
			return 10
		end;
	};
	[32]={
		name="Heavy";
		category="DPS/Tank";
		equips="m134";
		noknife=true;
		weapon=90;
		speed=7;
		health=250;
	};	
	[33]={
		name="Rambo";
		category="DPS";
		equips="m249";
		melee="machete";
		canpick=true;
		picklist={69};
		desc="Pick your machete to recover health.";
		
		ondie=function(uid)
			for index,id in pairs(item(0,"table")) do
				if item(id,"type")==69 then
					parse('removeitem '..id)
				end
			end
		end,
		onpick=function(uid,item_id,item_type)
			local itemlist=playerweapons(uid)
			for _,id in pairs(itemlist) do
				if id==69 then
					return 4
				end	
			end

			if item_type==69 then
				local h=math.random(35,50)
				cm.sethealth(uid,player(uid,"health")+h)
				cm.healeffect(uid,h)
				cm.healindicator(uid,uid,h)
			end	
		end;
		abilitydesc="Throw your machete.",
		onability=function(uid)	
			local itemlist=playerweapons(uid)
			for _,id in pairs(itemlist) do
				if id==69 then
					cm.throwmachete(uid)
					return 4
				end	
			end
			return -1,"No machete in hand!"
		end;
	};
	
	[34]={
		name="Scout";
		category="DPS";
		equips="scout,elite"; 
	};
	
	[35]={
		name="Paladin";
		category="Tank";
		equips="shield";
		health=200;
		melee="machete";
		weapon=41;
		desc="Defend projectiles using your shield.";
		ondamagetaken = function(uid,vid,weapon,hpdmg)
			if player(uid,"weapon")==41 and weapon==256 then
				return false
			end
		end
	};
	
	[36]={
		name="Nemesis";
		category="Tank";
		equips="gut bomb";
		health=220;
		inf=86;
		regen=5;
		melee="claw";
		--skin="<spritesheet:gfx/npc/zombie.bmp:32:32:m>";
		--skinframe=1;
		--skinscale=1.1;
		onattack = function(uid)
			cm.setweapon(uid,78)
		end;
		
		abilitydesc="Throw gut bombs at everyone on your radius";
		onability=function(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			local t = closeplayers(x,y,96)
			for k,vid in ipairs(t) do
				if vid ~= uid and not cm.sameteam(uid,vid) then
					local vx,vy,vtx,vty=cm.pos(vid)
					local angle=math.deg(math.atan2(y-vy,x-vx))
					cm.spawnprojectile(uid,86,x,y,180,angle-90)
				end
			end
			cm.spawnprojectile(uid,86,x,y,180,0)
			cm.spawnprojectile(uid,86,x,y,180,90)
			cm.spawnprojectile(uid,86,x,y,180,180)
			cm.spawnprojectile(uid,86,x,y,180,270)
			return 8
		end;
	};
	
	[37]={
		name="Stalker";
		category="DPS";
		health=200;
		speed=5;
		melee="machete";
		onsecond=function(uid)
			local x,y,tx,ty,rot=cm.pos(uid)

			if not tile(tx,ty,"walkable") then
				if player(uid,"health")>0 and player(uid,"health") < player(uid,"maxhealth") then
					local h=math.random(1,8)
					cm.sethealth(uid,player(uid,"health")+h)
					cm.healeffect(uid,h)
					cm.healindicator(uid,uid,h)
				end	
			end
		end;
		abilitydesc="Small leap through walls";
		onability=function(uid)
			return 0
		end,
		onabilitypressed=function(uid)
			local x,y,tx,ty,rot=cm.pos(uid)

			if rot <= 90 then 
				rot=rot+360 
			end 
			local angle=math.rad(math.abs(rot+90))-math.pi 
			local x=x+math.cos(angle)*16 
			local y=y+math.sin(angle)*16 
			
			if x>0 and y>0 and x<map("xsize")*32 and y<map("ysize")*32 then 
				parse("setpos "..uid.." "..x.." "..y) 
			end 
			return 0
		end;
	};
	
	[38]={
		name="Tesla";
		category="DPS";
		equips="92";
	};
	
	[39]={
		name="Builder";
		category="Support";
		melee="wrench";
		candrop=true;
		canpick=true;
		health=150;
		onbuild=function(uid,type,x,y)
			if type==20 or type==21 then return 0 end
			
			local px,py=player(uid,"tilex"),player(uid,"tiley")

			if (cm.builddata[type] and math.abs(x-px)<=1 and math.abs(y-py)<=1) then
				if cm.building_allowed[type] then
					if cm.objectamount(uid,type) >= cm.building_allowed[type] then
						cm.popup(uid,"Cannot build more than "..cm.building_allowed[type].." of this building",91)
						return 1
					end	
				end
				cm.spawnobject(type,x,y,0,0,player(uid,"team"),uid)
			end
			return 1
		end;
	};
	
	[40]={
		name="Laser";
		category="DPS";
		equips="laser";
	};
	[41]={
		name="SWAT";
		category="DPS";
		equips="20,79,60,56,52";
	};
	[42]={
		name="Rocket";
		category="DPS";
		equips="48";
	};
	[43]={
		name="Sniper";
		category="DPS";
		equips="35";
	};
	[44]={
		name="Digger";
		category="Support";
		health=150;
		melee="chainsaw";
		desc="Use chainsaw to dig.";
		speed=3;
		abilitydesc="Spawn a wall at mouse position.";
		onability=function(uid)
			local mx,my=cm.mouse(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			local px,py = math.floor(mx/32), math.floor(my/32)
			
			
			if px >= map("xsize") or px < 0 
			or py >= map("ysize") or py < 0 then
				return -1,"Out of bounds!"
				
			end
			
			if not tile(px,py,"walkable") then
				return -1,"Cannot place wall here!"
			end
			
			local tile_id = tile(px,py,"originalframe")
			
			parse("settile "..px.." "..py.." 4")
			timerex(2000,parse,1,"settile "..px.." "..py.." "..tile_id)
			
			return 8
		end;
		
		onattack=function(uid)
			local x,y,tx,ty,rot = cm.pos(uid)
			if not tile(tx,ty,"walkable") then
				local tile_id = tile(tx, ty, "originalframe")
				parse("settile "..tx.." "..ty.." 1")

				timerex(2000, cm.regen_wall, 1, uid, tx, ty, tile_id)

				local hpdmg = math.random(1,3)
				cm.sethealth(uid,player(uid,"health")+hpdmg)
				cm.healeffect(uid,hpdmg)		
				cm.healindicator(uid,uid,hpdmg)				
			else
				local angle=math.rad(math.abs(rot +90))-math.pi
				local px = math.floor( (x + math.cos(angle)*32)/32 )
				local py = math.floor( (y + math.sin(angle)*32)/32 )
				local tile_id = tile(px, py, "originalframe")
				local tile_id_feet = tile(tx,ty,"frame")
				if px >= map("xsize") or px < 0 
				or py >= map("ysize") or py < 0 then
					return -1,"Out of bounds!"
					
				end
				if not tile(px,py,"walkable") and tile(px, py, "frame") ~= 0 then
					parse("settile "..px.." "..py.." "..tile_id_feet)
					timerex(2000, cm.regen_wall, 1, uid, px, py, tile_id)
					
					local hpdmg = math.random(1,5)
					cm.sethealth(uid,player(uid,"health")+hpdmg)
					cm.healindicator(uid,uid,hpdmg)
					cm.healeffect(uid,hpdmg)	
					--timerex(2000,parse,1,"settile "..px.." "..py.." "..tile_id)
				end	
			end
		end;
	};	
	
}

cm.botclass = {21,29,30,31}

function cm.getclass(uid)
	return cm.classdata[cm.player[uid]]
end	
-----------------------------------------------------------------------------------------------


if __timerArg == nil then -- Checar se o timerex está carregado.
	__timerArg={}

	function timerex(ms,func,count,...)
		count=count or 1
		if count==0 then count=-1 end
		local tmpfunc,id="",0
		for i=1,8 do
			local rand=math.random(0,2)
			if rand==0 then
				tmpfunc=tmpfunc..string.char(math.random(65,90))
			elseif rand==1 then
				tmpfunc=tmpfunc..string.char(math.random(97,112))
			else
				tmpfunc=tmpfunc..math.random(0,9)
			end
		end
		
		table.insert(__timerArg,{type(func),tmpfunc,func,{...},count})
		for n,v in pairs(__timerArg) do
			if v[2]==tmpfunc then
				id=n
				break
			end
		end
		
		_G[tmpfunc]=function(id)
			--print("function call ID "..id)
			id=tonumber(id)
			if __timerArg[id][1]=="function" then
				__timerArg[id].r=__timerArg[id][3](unpack(__timerArg[id][4]))
			elseif __timerArg[id][1]=="string" or __timerArg[id][1]=="number" then
				__timerArg[id].r=loadstring("return "..__timerArg[id][3].."(unpack(__timerArg["..id.."][4]))")()
			end
			__timerArg[id][5]=__timerArg[id][5]-1
			if id and __timerArg[id] and __timerArg[id][5]==0 then
				_G[__timerArg[id][2]]=nil
				__timerArg[id]=nil
			end
		end
		--print(tmpfunc)
		timer(ms,tmpfunc,id,count)
		return id
	end
	
	function freetimerex(id)
		if __timerArg[id]~=nil then
			freetimer(__timerArg[id][2],id)
			_G[__timerArg[id][2]]=nil
			__timerArg[id]=nil
			return true
		end
		return false
	end
end	

function math.angle_pos(x,y,rot,dist)
	local dist=dist or 32
	if rot<-90 then rot=rot+360 end 
	angle=math.rad(math.abs(rot +90))-math.pi
	x=x+math.cos(angle)*dist
	y=y+math.sin(angle)*dist
	
	return x,y
end

function cm.split(e, sep)
    local result = {}  
	e:gsub("[^".. sep .."]+", function(s) table.insert(result, s:match("^%s*(.-)%s*$")) end)
    return result
end

function cm.equip(uid,...) 
	local arg = {...};
	for i,wep in ipairs(arg) do 
		if type(wep)=="string" then
			for name,wepid in pairs(cm.cs2ditemdata) do
				if wep:lower()==name:lower() then
					parse("equip "..uid.." "..wepid) 
					parse("setweapon "..uid.." "..wepid)
					break
				end
			end
		elseif type(wep)=="number" then
			if tonumber(wep)< 91 then
				parse("equip "..uid.." "..wep) 
				parse("setweapon "..uid.." "..wep)
			end	
		end	
	end 
end

function cm.customkill(killer,weaponname,victim)
	parse("customkill "..killer.." \""..weaponname.."\" "..victim)
end

function cm.killplayer(player)
	parse("killplayer "..player)
end

function cm.effect(e,x,y,p1,p2,r,g,b)
	parse('effect "'..e..'" '..x..' '..y..' '..p1..' '..p2..' '..r..' '..g..' '..b)
end

function cm.explosion(x,y,size,damage,sourceplayer)
	parse("explosion "..x.." "..y.." "..size.." "..damage.." "..sourceplayer)
end

function cm.hudtxt(id,text,x,y,align,valign,size)
	local align=align or ""
	local valign=valign or ""
	local size=size or ""
	local x = x or 0
	local y = y or 0
	parse('hudtxt '..id..' "'..text..'" '..x..' '..y..' '..align..' '..valign..' '..size)
end

function cm.hudtxt2(player,id,text,x,y,align,valign,size)
	local align=align or ""
	local valign=valign or ""
	local size=size or ""
	local x = x or 0
	local y = y or 0
	parse('hudtxt2 '..player..' '..id..' "'..text..'" '..x..' '..y..' '..align..' '..valign..' '..size)
end

function cm.hudtxtalphafade(player,id,duration,alpha)
	parse("hudtxtalphafade "..player.." "..id.." "..duration.." "..alpha)
end

function cm.hudtxtcolorfade(player,id,duration,red,green,blue)
	parse("hudtxtcolorfade "..player.." "..id.." "..duration.." "..red.." "..green.." "..blue)
end

function cm.hudtxtmove(player,id,duration,x,y)
	parse("hudtxtmove "..player.." "..id.." "..duration.." "..x.." "..y)
end

function cm.removeitem(item)
	parse("removeitem "..item)
end

function cm.setarmor(player,armor)
	parse("setarmor "..player.." "..armor)
end

function cm.setammo(player,weapon,ammoin,ammo)
	parse("setammo "..player.." "..weapon.." "..ammoin.." "..ammo)
end

function cm.setmoney(player,money)
	parse("setmoney "..player.." "..money)
end

function cm.setdeaths(player,deaths)
	parse("setdeaths "..player.." "..deaths)
end

function cm.sethealth(player,health)
	parse("sethealth "..player.." "..health)
end

function cm.setmaxhealth(player,maxhealth)
	parse("setmaxhealth "..player.." "..maxhealth)
end

function cm.setpos(player,x,y)
	parse(string.format('setpos %s %s %s',player,x,y))
end

function cm.setweapon(player,weapon)
	parse("setweapon "..player.." "..weapon)
end

function cm.shake(player,power)
	parse("shake "..player.." "..power)
end

function cm.spawnitem(item,x,y)
	parse("spawnitem "..item.." "..x.." "..y)
end

function cm.spawnnpc(_type,x,y,rot)
	parse("spawnnpc ".._type.." "..x.." "..y.." "..rot)
end

function cm.spawnobject(_type,x,y,rot,mode,team,player)
	parse("spawnobject ".._type.." "..x.." "..y.." "..rot.." "..mode.." "..team.." "..player)
end

function cm.spawnplayer(player,x,y)
	parse("spawnplayer "..player.." "..x.." "..y)
end

function cm.spawnprojectile(uid,_type,x,y,dist,rot) 
	parse("spawnprojectile "..uid.." ".._type.." "..x.." "..y.." "..dist.." "..rot)
end 

function cm.speedmod(player,speedmod)
	parse("speedmod "..player.." "..speedmod)
end

function cm.strip(player,weapon)
	parse("strip "..player.." "..weapon)
end

function cm.sound(soundfile)
	parse("sv_sound "..soundfile)
end

function cm.sound2(player,soundfile)
	parse("sv_sound2 "..player.." "..soundfile)
end

function cm.soundpos(str,x,y)
	parse('sv_soundpos "'..str..'" '..x..' '..y)
end

function cm.damageobject(oid,damage,uid)
	parse("damageobject "..oid.." "..damage.." "..uid)
end

cm.cs2ditemdata={
["USP"]=1;
["GLOCK"]=2;
["DEAGLE"]=3;
["P228"]=4;
["ELITE"]=5;
["FIVE-SEVEN"]=6;
["M3"]=10;
["XM1014"]=11;
["MP5"]=20;
["TMP"]=21;
["P90"]=22;
["MAC 10"]=23;
["UMP45"]=24;
["AK-47"]=30;
["SG552"]=31;
["M4A1"]=32;
["AUG"]=33;
["SCOUT"]=34;
["AWP"]=35;
["G3SG1"]=36;
["SG550"]=37;
["GALIL"]=38;
["FAMAS"]=39;
["M249"]=40;
["TACTICAL SHIELD"]=41;
["SHIELD"]=41;
["LASER"]=45;
["FLAMETHROWER"]=46;
["RPG LAUNCHER"]=47;
["ROCKET LAUNCHER"]=48;
["GRENADE LAUNCHER"]=49;
["HE"]=51;
["FLASHBANG"]=52;
["SMOKE GRENADE"]=53;
["SMOKE"]=53;
["FLARE"]=54;
["DEFUSE KIT"]=56;
["KEVLAR"]=57;
["KEVLAR+HELM"]=58;
["NIGHT VISION"]=59;
["GAS MASK"]=60;
["PRIMARY AMMO"]=61;
["SECONDARY AMMO"]=62;
["MEDIKIT"]=64;
["BANDAGE"]=65;
["COINS"]=66;
["MONEY"]=67;
["GOLD"]=68;
["MACHETE"]=69;
["GAS GRENADE"]=72;
["GAS"]=72;
["MOLOTOV COCKTAIL"]=73;
["MOLOTOV"]=73;
["WRENCH"]=74;
["SNOWBALL"]=75;
["AIR STRIKE"]=76;
["MINE"]=77;
["CLAW"]=78;
["LIGHT ARMOR"]=79;
["ARMOR"]=80;
["HEAVY ARMOR"]=81;
["MEDIC ARMOR"]=82;
["SUPER ARMOR"]=83;
["STEALTH SUIT"]=84;
["STEALTH ARMOR"]=84;
["STEALTH"]=84;
["CHAINSAW"]=85;
["GUT BOMB"]=86;
["LASER MINE"]=87;
["PORTAL GUN"]=88;
["PORTAL"]=88;
["SATCHEL CHARGE"]=89;
["SATCHEL"]=89;
["M134"]=90;
["FN F2000"]=91;
["TESLA"]=92;
}

cm.builddata = {"Barricade","Barbed Wire","Wall I","Wall II","Wall III","Gate Field","Dispenser","Turret","Supply",[13] = "Teleporter Entrance",[14] = "Teleporter Exit"}
-----------------------------------------------------------------------------------------------

function cm.los(x0,y0,x1,y1, callback)
	local x0 = math.floor(x0)
	local y0 = math.floor(y0)
	local x1 = math.floor(x1)
	local y1 = math.floor(y1)

	local sx,sy,dx,dy
	local lim=0
	if x0 < x1 then
		sx = 1
		dx = x1 - x0
	else
		sx = -1
		dx = x0 - x1
	end
	if y0 < y1 then
		sy = 1
		dy = y1 - y0
	else
		sy = -1
		dy = y0 - y1
	end
	local err, e2 = dx-dy, nil
	if not callback(x0, y0) then return false end
	while not(x0 == x1 and y0 == y1) do
		e2 = err + err
		if e2 > -dy then
			err = err - dy
			x0 = x0 + sx
		end
		if e2 < dx then
			err = err + dx
			y0 = y0 + sy
		end
		lim=lim+1
		if not callback(x0, y0) or lim>320 then return false end
	end
	return true
end

function cm.line(x0,y0,x1,y1,callback)
	local points = {}
	local count = 0
	local result = cm.los(x0,y0,x1,y1, function(x,y)
		if callback and not callback(x,y) then return false end
		count = count + 1
		points[count] = {x,y}
		return true
	end)
	return points, result
end

local function line(x0,y0,x1,y1,callback)
	local points = {}
	local count = 0
	local result = cm.los(x0,y0,x1,y1, function(x,y)
		if callback and not callback(x,y) then return false end
		count = count + 1
		points[count] = {x,y}
		return true
	end)
	return points, result
end


function cm.linemap(x,y)
     if tile(x,y,"walkable") then
          return true
     end
     return false
end

function cm.linemap_px(x,y)
	local square=12
	local x0,y0 = math.floor((x-square)/32),math.floor((y-square)/32)
	local x1,y1 = math.floor((x+square)/32),math.floor((y-square)/32)
	local x2,y2 = math.floor((x+square)/32),math.floor((y+square)/32)
	local x3,y3 = math.floor((x-square)/32),math.floor((y+square)/32)

	if tile(x0,y0,"walkable")
	and tile(x1,y1,"walkable")
	and tile(x2,y2,"walkable")
	and tile(x3,y3,"walkable")
	then
		return true
	end
	return false	
end

function cm.raytrace(x1,y1,x2,y2)
	if x1==x2 and y1==y2 then return false end

	-- Use bresenham code to calculate all points.
	local data, status = cm.line(x1,y1,x2,y2,function(x,y)
		local tile_x, tile_y = math.floor( x/32 ),math.floor( y/32 )
		if tile(tile_x,tile_y,"wall") then
			-- Colision in wall
			--cm.effect("colorsmoke",x,y,1,1,0,0,0)
			return false,x,y
		end
		return true
	end)
	
	-- Check in which point happened the colision
	if status then
	else
		local col_x,col_y
		local dist
		if #data > 0 then
			col_x = data[#data][1] 
			col_y = data[#data][2]

			dist = math.sqrt( (col_x - x1)^2 + (col_y - y1)^2 )
		else
			-- Sometimes data size is 0, this is a backup for that
			dist = 1
		end
		
		--[[
		local angle = math.deg( math.atan2(y1 - y2, x1 - x2) ) - 90
		if angle < -90 then
			angle = angle+360
		end
		--]]
		local angle = math.deg( math.atan2(y1 - y2, x1 - x2) )

		return col_x,col_y,angle
	end
end	


function cm.isInside(x1,y1,x2,y2,x3,y3,x4,y4,px,py)
	local function rightp(ax,ay,bx,by,cx,cy)
		return (bx - ax)*(cy - ay) > (by - ay)*(cx - ax)
	end	
	local a,b,c,d = rightp(x1,y1,x2,y2,px,py),rightp(x2,y2,x3,y3,px,py),rightp(x3,y3,x4,y4,px,py),rightp(x4,y4,x1,y1,px,py)
	if a and b and c and d then
		return true
	end
	return false	
end

function cm.sameteam(id1,id2)
	if game("sv_gamemode")=="1" then
		if id1==id2 then 
			return true
		end	
		return false
	end
	return player(id1,"team")==player(id2,"team")
end

function cm.angle(uid,dist,offset)
	local x,y,rotation = player(uid,"x"),player(uid,"y"),player(uid,"rot")
	local offset = offset or 0
	if rotation <= 90 then 
		rotation=rotation+360 
	end 
	rotation = rotation + offset
	local angle = math.rad(math.abs(rotation+90))-math.pi 
	local mx = x + math.cos(angle)*dist
	local my = y + math.sin(angle)*dist
	
	return x,y,rotation,mx,my
end

function cm.pos(uid)
	if uid > 32 then
		local oid = uid-32
		return object(oid,"x"),object(oid,"y"),object(oid,"tilex"),object(oid,"tiley"),object(oid,"rot")
		-- get a object id from player id offset.
	else
		return player(uid,"x"),player(uid,"y"),player(uid,"tilex"),player(uid,"tiley"),player(uid,"rot")
	end	
end

function cm.mouse(uid)
	if player(uid,"bot") then
		local target = ai_findtarget(uid)
		
		if player(target,"exists") then
			return player(target,"x"),player(target,"y")
		else
			return player(uid,"x"),player(uid,"y")
		end
	end
	return player(uid,"mousemapx"),player(uid,"mousemapy")
end

function cm.floor(x,y)
	return math.floor(x/32),math.floor(y/32)
end

function cm.color(r,g,b)
	return string.format("%03d%03d%03d",r,g,b)
end

-----------------------------------------------------------------------------------------------------
function cm.genericprojectile(uid,frame,offset)
	local frame=frame or 1
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end


function cm.arrow(uid,frame,offset)
	local frame=frame or 1
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.icearrow(uid,frame,offset)
	local frame=frame or 1
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
	local damage = math.random(12,25)	
		local x,y = cm.pos(vid)
		cm.slow(vid)
		
		cm.customdamage(uid,vid,damage,"Ice Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.effect("colorsmoke",x,y,5,5,255,255,255)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("colorsmoke",x,y,5,5,255,255,255)
	end
	
	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,1,150,150,255)
		return 3
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.frostarrow(uid,frame,offset)
	local frame=frame or 1
	local delay = 1500
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(30,50)
		local x,y = cm.pos(vid)
		cm.customdamageaoe(uid,x,y,64,damage,"Frost Arrow",cm.slow)
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.effect("flare",x,y,100,50,150,150,255)
	end
	
	local colision_func = function(x,y,img)
		local uid = img.source
		local damage = math.random(30,50)	
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("flare",x,y,100,50,150,150,255)
		cm.customdamageaoe(uid,x,y,64,damage,"Frost Arrow",cm.slow)
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,150,150,255)
		return 1
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.firearrow(uid,frame,offset)
	local frame=frame or 1
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
	local damage = math.random(12,25)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Fire Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.effect("fire",x,y,7,7,255,255,255)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("fire",x,y,7,7,255,255,255)
	end
	
	local tail_func = function(x,y,img)
		cm.effect("colorsmoke",x,y,1,1,0,0,0)
		return 4
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.burnarrow(uid,frame,offset)
	local frame=frame or 1
	local delay = 1500
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(30,50)
		local x,y = cm.pos(vid)
		cm.customdamageaoe(uid,x,y,64,damage,"Burn Arrow")
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.effect("fire",x,y,50,20,150,150,255)
		
		cm.burn(vid,uid)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("fire",x,y,50,20,255,255,255)
	end
	
	local tail_func = function(x,y,img)
		--cm.effect("colorsmoke",x,y,1,1,0,0,0)
		cm.effect("fire",x,y,3,3,255,255,255)
		return 2
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.ichorarrow(uid,dmg)
	local tiles = 20
	local frame = 26
	local delay = 800
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	imagescale(s,1.5,1.5)
	--imagecolor(s,214,040,079)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(dmg-10,dmg+10)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Ichor Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		return true
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,255,255,000)
		return 5
	end
	cm.create_projectile(uid,s,damage_func,nil,tail_func,nil)
end

function cm.ichorcharge(uid)
	local frame = 26
	local delay = 1500
	local s = image(CM_SPRITESHEET_PATH,1,0,uid+132)
	
	imageframe(s,frame)
	--imagecolor(s,214,040,079)
	imagescale(s,1,1)
	tween_scale(s,delay-500,1.5,1.5)
	timerex(delay,freeimage,1,s)
	
	local tail_func = function(x,y,img)
		local released = cm.isreleased(img.source,"E")
		if released then
			cm.effect("flare",x,y,5,16,255,255,000)
			cm.ichorarrow(uid,math.floor( 50 + released*30 ))
			
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.effect("flare",x,y,5,16,255,255,000)
		cm.ichorarrow(uid,80)
	end
	cm.create_projectile(uid,s,nil,nil,tail_func,nil,end_func)
end

function cm.gladius(uid)
	local delay = 500
	local x,y,rotation,mx,my = cm.angle(uid,32*9,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,3)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(35,45)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Gladius")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.gladiustp(uid)
	local delay = 800
	local x,y,rotation,mx,my = cm.angle(uid,32*6,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,3)
	imagecolor(s,255,50,50)
	imagepos(s,x,y,rotation)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
	local damage = math.random(60,65)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Gladius")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.effect("colorsmoke",x,y,5,5,255,0,0)
			cm.setpos(uid,tx*32+16,ty*32+16)
		end
	end
	
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
		
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.effect("colorsmoke",tx*32+16,y*32+16,5,5,255,0,0)
			cm.setpos(uid,tx*32+16,ty*32+16)
		end
	end
	
	local tail_func = function(x,y,img)
		if cm.isreleased(img.source, "E") then
			local tx,ty = cm.floor(x,y)
			if tile(tx,ty,"walkable") then
				cm.setpos(uid,x,y)
				cm.effect("colorsmoke",x,y,5,5,255,0,0)
			end

			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.setpos(uid,x,y)
			cm.effect("colorsmoke",x,y,5,5,255,0,0)
		end
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,end_func)
end

function cm.unlimitedblade(uid)
	local x,y,rot = cm.pos(uid)
	local mx, my = cm.mouse(uid)
	if rot <= 90 then 
		rot=rot+360 
	end 
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Unlimited Blade Works")
		cm.effect("colorsmoke",x,y,1,1,128,0,0)
		
		cm.soundpos("weapons/machete_hit.wav",x,y)
	end
	
	local s = {}
	for i = 1,8 do
		local angle=math.rad(math.abs( i*45 ))-math.pi 
		local sx=x+math.cos(angle)*32*4  -- Spread
		local sy=y+math.sin(angle)*32*4
	
		s[i] = image(CM_SPRITESHEET_PATH,x,y,3)
		imageframe(s[i],3)
		imagepos(s[i],x,y, (i) * 45 - 90)
		tween_move(s[i],300,sx,sy)

		local target = math.deg( math.atan2(sy - my, sx - mx) ) + 90
		local angle2 = math.rad( math.abs(target+90) ) - math.pi
		
		local dist = math.sqrt( (sx - mx)^2 + (sy - my)^2 ) + 40
		
		local tx=sx - math.cos(angle2)*dist  -- Target
		local ty=sy - math.sin(angle2)*dist
		
		timerex(300,tween_rotate,1,s[i],100,target + 180)
		--timerex(400,tween_move,1,s[i],200,tx,ty)
		timerex(400,tween_move,1,s[i],200,tx,ty)
		timerex(400,tween_alpha,1,s[i],400,0)
		timerex(800,freeimage,1,s[i])
		
		cm.create_projectile(uid,s[i],damage_func)
	end
end

function cm.windblow(uid)
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,2)
	imagescale(s,0.3,0.3)
	
	tween_alpha(s,math.floor(delay * 0.8),0)
	tween_scale(s,math.floor(delay * 0.8),0.6,0.6)
	tween_move(s,delay,mx,my)
	
	tween_rotateconstantly(s,10)
	
	timerex(delay,freeimage,1,s)
	
	local colision_func = function(x,y,img)
		tween_move(img.id,0,x,y)
		img.calculation = false
		return true
	end
	
	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,50,150,150,255)
		local damage = math.random(3,5)
		cm.customdamageaoe(uid,x,y,52,damage,"Tornado")
		return 3
	end
	cm.create_projectile(uid,s,nil,colision_func,tail_func)
end

function cm.windsurge(uid)
	local delay = 1000
	--local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local x,y = cm.mouse(uid)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,2)
	imagepos(s,x,y,0)
	
	tween_alpha(s,math.floor(delay * 0.8),0)
	tween_rotateconstantly(s,12)
	
	timerex(delay,freeimage,1,s)

	
	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,50,150,150,255)
		local damage = math.random(2,4)
		cm.customdamageaoe(uid,x,y,52,damage,"Tornado")
		return 3
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
end

function cm.tornado(uid)
	local delay = 1000
	--local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local x,y = cm.pos(uid)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,2)
	imagepos(s,x,y,0)
	
	tween_alpha(s,math.floor(delay * 0.8),0)
	tween_rotateconstantly(s,12)
	
	timerex(delay,freeimage,1,s)

	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,50,150,150,255)
		local damage = math.random(5,10)
		cm.customdamageaoe(uid,x,y,54,damage,"Tornado")
		return 3
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
end

function cm.lightbolt(uid)
	local delay = 800
	local x,y,rotation,mx,my = cm.angle(uid,32*12)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,27)
	imagealpha(s,0.6)
	timerex(delay,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)
	
	tween_rotateconstantly(s,5)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Light Bolt")
		
		cm.soundpos("env/laser.wav",x,y)
		
		cm.effect("flare",x,y,5,5,210,230,255)
		cm.blink2(x,y,210,230,255)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("env/laser.wav",x,y)

		cm.effect("flare",x,y,5,5,210,230,255)
		cm.blink2(x,y,210,230,255)
	end
	
	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,1,210,230,255)
		return 3
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.lightbind(uid)
	local delay = 700
	local x,y,rotation,mx,my = cm.angle(uid,32*8)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,65)
	imagepos(s,x,y,rotation)
	imageblend(s,1)
	
	tween_move(s,delay,mx,my)
	
	timerex(delay-50,tween_alpha,1,s,50,0)
	timerex(delay,freeimage,1,s)

	local damage_func = function(uid,vid)
		local x,y = cm.pos(vid)
		--cm.effect("flare",x,y,20,1,210,230,255)
		
		cm.bind(vid,uid)
		return true
	end
	
	local colision_func = function(x,y)
		cm.effect("flare",x,y,5,5,210,230,255)
	end
	
	local tail_func = function(x,y,img)
		cm.effect("flare",x,y,1,1,210,230,255)
		return 3
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.lightbeam(uid,tiles)
	local x,y,tx,ty,rot = cm.pos(uid)
	local l=image(CM_SPRITESHEET64,x,y,3)
	local tiles=tiles or 12
	local width = 0.75
	
	local r=255
	local g=255
	local b=255
	
	local delay = 500
	local angle = math.rad(math.abs(rot +90))-math.pi
	local tx = x + math.cos(angle)*(tiles-1)*16
	local ty = y + math.sin(angle)*(tiles-1)*16
	
	-- Extra tile of spacing
	bx = tx + math.cos(angle)*32
	by = ty + math.sin(angle)*32
	
	imageframe(l,13)
	imagepos(l,bx,by,rot)
	imageblend(l,1)
	imagecolor(l,r,g,b)
	imagescale(l,width,(tiles-1)/4)
	
	tween_scale(l,delay,0,(tiles-1)/4)
	tween_alpha(l,delay,0)
	timerex(600,freeimage,1,l)
	
	local cx = x + math.cos(angle)*16
	local cy = y + math.sin(angle)*16
	local c=image(CM_SPRITESHEET64,x,y,3)
	imageframe(c,14)
	imagepos(c,cx,cy,rot)
	imageblend(c,1)
	imagecolor(c,r,g,b)
	imagescale(c,width,0.25)
	
	tween_scale(c,delay,0,0.25)
	tween_alpha(c,delay,0)
	
	timerex(600,freeimage,1,c)
end

function cm.linecolision(x,y,rot,width,reach)
	local reach=reach or 8
	local width=width or 1
	local rot=rot or 0
	local angle=math.rad(math.abs(rot+90))-math.pi
	local cosx=math.cos(angle)
	local siny=math.sin(angle)
	local ax=x+math.cos(angle+90)*32*width
	local ay=y+math.sin(angle+90)*32*width
	local bx=x+math.cos(angle-90)*32*width
	local by=y+math.sin(angle-90)*32*width
	local cx=bx+cosx*32*(reach+1)
	local cy=by+siny*32*(reach+1)
	local dx=ax+cosx*32*(reach+1)
	local dy=ay+siny*32*(reach+1)

	local t={}
	for index,vid in pairs(player(0,"tableliving")) do
		local vx,vy=cm.pos(vid)
		if cm.isInside(ax,ay,bx,by,cx,cy,dx,dy,vx,vy) then
			table.insert(t,vid)
		end
	end
	return t
end

function cm.iceshard(uid,rotation)
	local frame = 7
	local delay = 600
	local tiles = 7
	local rotation = rotation or 0
	local x,y = cm.pos(uid)
	local angle = math.rad(math.abs(rotation+90))-math.pi 
	local mx = x + math.cos(angle)*tiles*32
	local my = y + math.sin(angle)*tiles*32
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
	local damage = math.random(7,15)	
		local x,y = player(vid,"x"),player(vid,"y")
		cm.customdamage(uid,vid,damage,"\169190255255Ice Shard")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		return true
	end
	cm.create_projectile(uid,s,damage_func,nil,nil)
end

function cm.iceboulder(uid)
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*15)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,41)
	imagepos(s,x,y,rotation)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,8)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		cm.bind(vid,uid)
		
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169190255255Ice Boulder")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.effect("colorsmoke",x,y,5,5,190,255,255)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,100,100,255)
		cm.effect("colorsmoke",x,y,5,5,190,255,255)
	end
	
	local tail_func = function(x,y)
		cm.effect("colorsmoke",x,y,1,1,190,255,255)
		return 5
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.icestorm(uid)
	local delay = 1000
	--local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local x,y = cm.pos(uid)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,3)
	imagepos(s,x,y,0)
	tween_alpha(s,math.floor(delay * 0.8),0)
	tween_scale(s,delay,1.2,1.2)
	timerex(delay,freeimage,1,s)
	
	
	local damage = math.random(15,40)
	cm.customdamageaoe(uid,x,y,64,damage,"\169190255255Ice Storm",cm.bind)	
	cm.effect("colorsmoke",x,y,20,32,190,255,255)
end

function cm.darkmatter(uid)
	local delay = 1600
	local x,y,rotation,mx,my = cm.angle(uid,32*20)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,84)
	
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,10)
	
	local damage_func = function(uid,vid)
		local damage = math.random(30,60)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169144113252Dark Matter")
		
		cm.soundpos("env/laser.wav",x,y)
		cm.darkblink(x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("env/laser.wav",x,y)
		cm.darkblink(x,y)
	end
	
	local tail_func = function(x,y)
		cm.effect("colorsmoke",x,y,1,1,0,0,40)
		return 7
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.eclipse(uid)
	local x,y,tx,ty,rot = cm.pos(uid)
	--local s=image(CM_SPRITESHEET64,x,y,0)
	local s=image(CM_SPRITESHEET256,x,y,0)
	imageframe(s,21)
	imageblend(s,2)
	imagescale(s,0,0)
	--imagecolor(s,0,0,0)
	tween_scale(s,300,0.8,0.8)
	timerex(4500,tween_scale,1,s,500,0,0)
	timerex(5000,freeimage,1,s)
	

	
	local tail_func = function(x,y,img)
		local uid = img.source
		local damage = math.random(2,5)
		local victim = cm.customdamageaoe(uid,x,y,100,damage,"\169144113252Eclipse")
		
		for index, vid in ipairs(victim) do	
			local vx,vy = cm.pos(vid)
			cm.effect("colorsmoke",vx,vy,1,1,0,0,0)
		end
		return 10
	end
	
	cm.create_projectile(uid,s,nil,nil,tail_func)
end

function cm.nebula(uid)
	local delay = 3000
	local x,y = cm.mouse(uid)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,4)
	imagepos(s,x,y,0)
	imagealpha(s,0)
	imagescale(s,1,1)
	tween_alpha(s,300,0.8)
	tween_rotateconstantly(s,12)
	timerex(2500,tween_alpha,1,s,500,0)
	timerex(delay,freeimage,1,s)

	
	local tail_func = function(x,y,img)
		local mx,my = cm.mouse(img.source)
		local damage = math.random(1,2)
		cm.customdamageaoe(uid,x,y,52,damage,"\169144113252Nebula")
		
		cm.effect("flare",x,y,5,70,144,3,252)
		
		tween_move(img.id,300,mx,my)
		return 5
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
end


function cm.fissure(uid)
	local delay = 1000
	--local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local x,y = cm.pos(uid)
	local s = image(CM_SPRITESHEET64,x,y,0)
	
	imageframe(s,16)
	imagepos(s,x,y,0)
	
	tween_alpha(s,math.floor(delay * 0.8),0)
	tween_scale(s,delay,1.02,1.02)
	timerex(delay,freeimage,1,s)
	
	
	local damage = math.random(10,20)
	local victim = cm.customdamageaoe(uid,x,y,64,damage,"Fissure")	
	for index,vid in ipairs(victim) do
		cm.shake(vid,30)
	end
	cm.shake(uid,15)
	--cm.effect("colorsmoke",x,y,20,32,255,255,255)
end

function cm.boulder(uid)
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*12)
	local s = image(CM_SPRITESHEET64,x,y,3)
	
	imageframe(s,17)
	imagepos(s,x,y,rotation)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,8)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
	local damage = math.random(10,20)	
		cm.bind(vid,uid)
	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Boulder")
		
		cm.soundpos("materials/stone"..math.random(1,3)..".wav",x,y)
		cm.effect("colorsmoke",x,y,1,32,255,255,255)
	end
	
	local colision_func = function(x,y,img)
		-- Assumindo que x-y é sempre walkable
		local tx,ty = cm.floor(x,y)
		
		-- Obtem os 8 tiles ao redor.
		local square = cm.tilearea(x,y,3)
		local tile_id_feet = tile(tx,ty,"frame")
		for index,v in ipairs(square) do
			if not tile(v[1],v[2],"walkable") then
				local tile_id_og = tile(v[1],v[2],"originalframe")
				parse("settile "..v[1].." "..v[2].." "..tile_id_feet)	
				timerex(3500, cm.regen_wall, 1, img.source, v[1], v[2], tile_id_og)
				cm.soundpos("materials/stone"..math.random(1,3)..".wav",v[1]*32,v[2]*32)
			end
		end
		
	
		--cm.soundpos("materials/stone"..math.random(1,3)..".wav",x,y)
		cm.effect("colorsmoke",x,y,50,32,255,255,255)
	end
	
	local tail_func = function(x,y)
		cm.effect("colorsmoke",x,y,3,3,255,255,255)
		return 6
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.kunai(uid,frame,offset)
	local frame=frame or 1
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(5,15)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Kunai")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.dissonance(uid)
	local frame = 53
	local delay = 600
	local x,y,rotation,mx,my = cm.angle(uid,32*10)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(5,15)	
		local x,y = cm.pos(vid)
		--[[
		local c = math.random(1,7)
		local clr = cm.color(CM_RAINBOW[c][1],CM_RAINBOW[c][2],CM_RAINBOW[c][3])
		cm.customdamage(uid,vid,damage,"\169"..clr.."Kunai")
		--]]
		cm.customdamage(uid,vid,damage,"Kunai")
		
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.afterimage(uid)
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.setpos(uid,tx*32+16,ty*32+16)
		end
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)

		cm.afterimage(uid)
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.setpos(uid,tx*32+16,ty*32+16)
		end
	end
	
	local tail_func = function(x,y)
		local c = math.random(1,7)
		cm.effect("flare",x,y,1,5,CM_RAINBOW[c][1],CM_RAINBOW[c][2],CM_RAINBOW[c][3])
	end
	
	local end_func = function(x,y)
		cm.afterimage(uid)
		local tx,ty = cm.floor(x,y)
		if tile(tx,ty,"walkable") then
			cm.setpos(uid,tx*32+16,ty*32+16)
		end
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,end_func)
end


function cm.shuriken(uid)
	local frame= 1
	local delay = 3500
	local x,y = cm.pos(uid)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,11)
	imagepos(s,x,y,0)
	tween_rotateconstantly(s,12)
	--timerex(2500,tween_alpha,1,s,500,0)
	timerex(delay,freeimage,1,s)

	
	local damage_func = function(uid,vid)
		local damage = math.random(25,35)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Shuriken")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
	end
	
	local tail_func = function(x,y,img)
		local mx,my = cm.mouse(img.source)
		tween_move(img.id,300,mx,my)
		return 5
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.bubble(uid,offset,tiles)
	local frame = 69
	local delay = 1000
	local tiles = tiles or 7
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagealpha(s,0.6)
	imagescale(s,0,0)
	
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,math.random(0,14)-7)
	tween_scale(s,150,1,1)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(10,20)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Bubble")
		
		cm.soundpos("env/laser.wav",x,y)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)

		tween_move(img.id,0,x,y)
		tween_scale(img.id,50,1.5,1.5)
		tween_alpha(img.id,100,0)
		
		img.active = false
		img.calculation = false
		return true
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end


function cm.bigbubble(uid,offset)
	local frame = 39
	local delay = 5000
	local x,y,rotation,mx,my = cm.angle(uid,32*5,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagealpha(s,0.6)
	imagescale(s,0,0)
	
	tween_scale(s,300,1,1)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s, math.random()*2 -1)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid,img)
		local damage = math.random(60,120)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Big Bubble")
		
		cm.soundpos("env/laser.wav",x,y)
		
		tween_move(img.id,0,img.x,img.y)
		tween_scale(img.id,50,1.5,1.5)
		tween_alpha(img.id,100,0)
		
		img.active = false
		img.calculation = false
		return true
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)
		
		tween_move(img.id,0,x,y)
		tween_scale(img.id,50,1.5,1.5)
		tween_alpha(img.id,100,0)
		
		img.active = false
		img.calculation = false
		return true
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.sulphurbubble(uid,offset)
	local frame = 70
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*7,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagealpha(s,0.6)
	imagescale(s,0,0)
	
	tween_scale(s,300,1,1)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,math.random(0,14)-7)
	
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(5,10)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169141208056Bubble")
		
		cm.soundpos("env/laser.wav",x,y)
		
		cm.poison(vid,uid,5)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)

		tween_move(img.id,0,x,y)
		tween_scale(img.id,50,1.5,1.5)
		tween_alpha(img.id,100,0)
		
		img.active = false
		img.calculation = false
		return true
	end
	
	local tail_func = function(x,y,img)
		cm.effect("colorsmoke",x,y,1,1,141,208,56)
		return 6
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.musicalnote(uid,offset)
	local frame = 62
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*7,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagealpha(s,0.5)
	imagescale(s,0,0)
	
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,math.random(0,14)-7)
	tween_scale(s,300,1,1)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(5,10)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Bubble")
		
		cm.soundpos("env/laser.wav",x,y)
		cm.effect("flare",x,y,10,5,74,107,172)
		
		return true
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)
		cm.effect("flare",x,y,10,5,74,107,172)

		return false
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,74,107,172)
		return 15
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.healingaura(uid)
	local delay = 3500
	local s = image(CM_SPRITESHEET64,0,0,100+uid)
	
	imageframe(s,15)
	imagealpha(s,0)
	imagecolor(s,0,200,0)
	imageblend(s,1)
	
	tween_alpha(s,300,0.6)
	tween_rotateconstantly(s,0.2)
	
	timerex(delay-500,tween_alpha,1,s,500,0)
	timerex(delay,freeimage,1,s)

	
	local tail_func = function(x,y,img)
		local uid = img.source
		local hp = math.random(4,10)
		for index,aid in ipairs(closeplayers(x,y,64)) do
			if cm.sameteam(aid,uid) then
				
				cm.sethealth(aid,player(aid,"health")+math.floor(hp))
				cm.healindicator(aid,uid,hp)
			end
		end
		return 15
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
end

function cm.sonicnote(uid,offset)
	local frame = 62
	local delay = 1200
	local x,y,rotation,mx,my = cm.angle(uid,32*12,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagealpha(s,0.7)
	imagecolor(s,0,200,0)
	imageblend(s,1)
	
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,math.random(0,14)-7)

	timerex(delay,freeimage,1,s)
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)
		cm.effect("flare",x,y,10,5,0,200,0)
	end
	
	local ally_func = function(uid,aid)
		local x,y = player(aid,"x"),player(aid,"y")
		cm.effect("flare",x,y,10,5,0,200,0)
		
		local hp = math.random(50,70)
		cm.sethealth(aid,player(aid,"health")+math.floor(hp))
		cm.healindicator(aid,uid,hp)
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,0,200,0)
		return 6
	end
	cm.create_projectile(uid,s,nil,colision_func,tail_func,ally_func)
end


function cm.whirlpool(uid)
	local delay = 1000
	local frame = 61
	local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s, frame)
	imagescale(s,0.3,0.3)
	
	tween_alpha(s,math.floor(delay * 1.5),0)
	tween_scale(s,math.floor(delay * 0.8),1.5,1.5)
	tween_move(s,delay,mx,my)
	
	tween_rotateconstantly(s,10)
	
	timerex(delay,freeimage,1,s)
	
	local colision_func = function(x,y,img)
		tween_move(img.id,0,x,y)
		img.calculation = false
		return true
	end
	
	local tail_func = function(x,y,img)
		local damage = math.random(5,25)
		cm.customdamageaoe(uid,x,y,20,damage,"\169105195193Whirlpool")
		return 4
	end
	cm.create_projectile(uid,s,nil,colision_func,tail_func)
end

function cm.shark(uid,offset)
	local frame = 82
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	tween_move(s,delay,mx,my)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		cm.slow(vid,uid)
		
		
		local damage = math.random(40,50)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169105195193Shark")
		
		cm.soundpos("env/laser.wav",x,y)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)

		tween_move(img.id,0,x,y)
		tween_scale(img.id,50,1.5,1.5)
		tween_alpha(img.id,100,0)
		
		img.active = false
		img.calculation = false
		return true
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,3,3,105,195,193)
		return 3
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end


function cm.waterstream(uid)
	local delay = 3000
	local x,y,rotation,mx,my = cm.angle(uid,32*5)
	local s = image(CM_SPRITESHEET64,x,y,0)
	
	imageframe(s,1)
	imagealpha(s,0)
	
	tween_move(s,delay,mx,my)
	tween_alpha(s,300,0.7)
	tween_rotateconstantly(s,-6)
	
	timerex(2500,tween_alpha,1,s,500,0)
	timerex(delay,freeimage,1,s)

	local tail_func = function(x,y,img)
		local damage = math.random(5,10)
		cm.customdamageaoe(uid,x,y,64,damage,"\169105195193Water Stream")
		return 5
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
	--cm.create_projectile(uid,s,nil,nil,nil)
end

function cm.petal(uid,offset,tiles)
	local frame = 72
	local delay = 400
	tiles = tiles or 10
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(10,20)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255131152Petal")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,255,131,152)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.petal2(uid,x,y,rotation,tiles)
	local frame = 72
	local delay = 500
	local tiles = tiles or 7
	local rotation = rotation or 0
	local angle = math.rad(math.abs(rotation+90))-math.pi 
	local mx = x + math.cos(angle)*tiles*32
	local my = y + math.sin(angle)*tiles*32
	
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(10,20)
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255131152Petal")
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,255,131,152)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.thorn2(uid,x,y,rotation,tiles)
	tiles = tiles or 7
	rotation = rotation or 0
	local frame = 74
	local delay = 500
	local angle = math.rad(math.abs(rotation+90))-math.pi 
	local mx = x + math.cos(angle)*tiles*32
	local my = y + math.sin(angle)*tiles*32
	
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(10,20)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255131152Thorn")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,255,131,152)
	end
	cm.create_projectile(uid,s,damage_func,colision_func)
end

function cm.flower(uid)
	local frame = 71
	local delay = 500
	local x,y,rotation,mx,my = cm.angle(uid,32*6,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	--imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,10)
	
	local damage_func = function(uid,vid)
	local damage = math.random(12,20)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255131152Flower")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		cm.effect("flare",x,y,5,5,255,131,152)
		
		return true
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("flare",x,y,5,5,255,131,152)
	end
	
	local tail_func = function(x,y,img)
		if cm.isreleased(img.source, "E") then
			cm.effect("flare",x,y,20,5,255,131,152)
			for angle=30,330,60 do
				cm.petal2(img.source,x,y,angle,6)
			end	
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.effect("flare",x,y,20,5,255,131,152)
		for angle=30,330,60 do
			cm.petal2(img.source,x,y,angle,6)
		end	
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,end_func)
end

function cm.thorn(uid)
	local frame = 73
	local delay = 500
	local x,y,rotation,mx,my = cm.angle(uid,32*6,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
	local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255131152Thorn")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		cm.effect("flare",x,y,5,5,255,131,152)
		
		return true
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("flare",x,y,5,5,255,131,152)
	end
	
	local tail_func = function(x,y,img)
		if cm.isreleased(img.source, "Q") then
			cm.effect("flare",x,y,20,5,255,131,152)

			cm.thorn2(img.source,x,y,img.rot,6)
			cm.thorn2(img.source,x,y,img.rot+30,6)
			cm.thorn2(img.source,x,y,img.rot-30,6)
			
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.effect("flare",x,y,20,5,255,131,152)

		cm.thorn2(img.source,x,y,img.rot,6)
		cm.thorn2(img.source,x,y,img.rot+30,6)
		cm.thorn2(img.source,x,y,img.rot-30,6)
	end

	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,end_func)
end

function cm.poisonarrow(uid,offset)
	local frame = 25
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*20,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		cm.poison(vid,uid)
		
		local damage = math.random(5,8)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169141208056Poison Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,141,208,56)
		cm.effect("flare",x,y,3,3,141,208,56)
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,141,208,56)
		return 5
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.poisonpotion(uid)
	local frame = 12
	local delay = 800
	local x,y,rotation,mx,my = cm.angle(uid,32*8,0)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,10)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
	local damage = math.random(12,20)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169141208056Poison Potion")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		cm.effect("flare",x,y,5,5,141,208,56)
		
		cm.poisonfield(uid,x,y)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("flare",x,y,5,5,141,208,56)
		cm.poisonfield(img.source,x,y)
	end
	
	local end_func = function(x,y,img)
		cm.effect("flare",x,y,5,5,141,208,56)
		cm.poisonfield(img.source,x,y)
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,nil,nil,end_func)
end

function cm.poisonfield(uid,x,y)
	local delay = 3000
	local s = image(CM_SPRITESHEET64,x,y,0)
	
	imageframe(s,9)
	imagepos(s,x,y,0)
	imagecolor(s,141,208,56)
	imagealpha(s,0)
	
	tween_alpha(s,300,0.5)
	timerex(2500,tween_alpha,1,s,500,0)
	timerex(delay,freeimage,1,s)

	
	local tail_func = function(x,y,img)
		
		for index,vid in ipairs(closeplayers(x,y,64)) do
			if img.source ~= vid then
				cm.poison(vid, img.source)
			end
		end
		cm.effect("flare",x,y,15,64,141,208,056)
		return 30
	end
	cm.create_projectile(uid,s,nil,nil,tail_func)
end


function cm.contaminate(uid)
	local x,y = cm.pos(uid)
	local victims = {}
	for index,vid in ipairs(closeplayers(x,y,256)) do
		if uid ~= vid then
			local data = cm.check_timed_effect(vid,"poison")
			if data then
				table.insert(victims,vid)
				
				local vx,vy = cm.pos(vid)
				local path = cm.line(x,y,vx,vy)
				
				for index,pixel in ipairs(path) do
					if index % 12 == 0 then
						local px,py = pixel[1],pixel[2]
						cm.effect("flare",px,py,1,1,141,208,56)	
					end	
				end
				
				cm.customdamage(uid, vid, data.stack_count*7 ,"\169141208056Poison")
				cm.effect("flare",vx,vy,15,32,141,208,56)
				cm.dispose_timed_effect(vid,"poison")
			end
		end
	end
	
	if #victims > 0 then
		-- Cooldown
		return 10
	else
		return 0,"No players in range"
	end
end

function cm.blood(uid)
	local frame = 75
	local delay = 800
	local tiles = 12
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	tween_move(s,delay,mx,my)
	
	timerex(delay,freeimage,1,s)
	
	
	if player(uid,"health") > 1 then
		cm.sethealth(uid, player(uid,"health") - 1)
	end
	
	local damage_func = function(uid,vid)
		local damage = math.random(30,50)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169162013029Blood")
		
		cm.soundpos("env/laser.wav",x,y)
		cm.effect("blood",x,y,5,math.random(0,360),0,0,0)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("env/laser.wav",x,y)
		cm.effect("blood",x,y,5,math.random(0,360),0,0,0)
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,162,13,29)
		return 6
	end
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.blood2(uid,x,y,rotation,tiles,damage)
	local frame = 75
	local delay = 300
	local tiles = tiles or 3
	local rotation = rotation or 0
	local angle = math.rad(math.abs(rotation+90))-math.pi 
	local mx = x + math.cos(angle)*tiles*32
	local my = y + math.sin(angle)*tiles*32
	
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_alpha(s,delay,0)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = damage
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169162013029Blood")
		
		cm.effect("blood",x,y,5,math.random(0,360),0,0,0)
	end
	
	cm.create_projectile(uid,s,damage_func,nil,nil)
end


function cm.bloodwave(uid)
	local frame = 42
	local delay = 1500
	local s = image(CM_SPRITESHEET_PATH,0,0,uid+132)
	
	imageframe(s,frame)
	imagecolor(s,162,013,029)
	imagescale(s,0.5,0.5)
	tween_rotateconstantly(s,-10)
	tween_scale(s,delay-500,1.5,1.5)
	timerex(delay,freeimage,1,s)
	
	cm.speedmod(uid,15)
	
	local tail_func = function(x,y,img)
		local released = cm.isreleased(img.source,"E")
		if released then
			cm.effect("colorsmoke",x,y,5,32,162,013,029)
			
			if player(uid,"health") > 9 then
				cm.sethealth(uid, player(uid,"health") - 20)
			end
			local radius = 24
			local damage = math.floor(30*released)
			for angle=0,360,radius do
				cm.blood2(img.source,x,y,angle,3,damage)
			end
			-- Restore speed
			local speed = cm.classdata[cm.player[img.source]].speed or 0
			cm.speedmod(img.source, speed )
			
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.effect("colorsmoke",x,y,5,32,162,013,029)
		if player(uid,"health") > 9 then
			cm.sethealth(uid, player(uid,"health") - 20)
		end
		local radius = 24
		for angle=0,360,radius do
			cm.blood2(img.source,x,y,angle,3,33)
		end
		-- Restore speed
		local speed = cm.classdata[cm.player[img.source]].speed or 0
		cm.speedmod(img.source, speed )
	end
	cm.create_projectile(uid,s,nil,nil,tail_func,nil,end_func)
end

function cm.leaf(uid,tiles,offset)
	local frame = 17
	local delay = 500
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,1)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	imagealpha(s,0.8)
	tween_move(s,delay,mx,my)
	
	timerex(2250,tween_alpha,1,s,250,0)
	timerex(2500,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(12,25)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169141208056Leaf")
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		return true
	end
	
	local colision_func = function(x,y,img)
		tween_move(img.id,0,x,y)
		img.calculation = false
		
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,141,208,56)
		
		return true
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,nil)
end

function cm.leafpull(uid)
	local leaves = 0
	local x,y,tx,ty,rot = cm.pos(uid)
	local victim_pulled = {}
	
	for key,img in pairs(cm.image_projectile) do
		-- 17 = leaf frame id
		local frame = imageparam(img.id,"frame") or 0
		
		if (frame+1) == 17 then
			
			local target = math.atan2(img.y - y, img.x - x)
			local target2 = math.deg ( target ) + 90
			local dist = math.floor( math.sqrt( ( img.x - x )^2 + ( img.y - y )^2 ) /32 )
			
			img.victim = {}
			img.onhit = nil
			img.oncolision = nil
			
			tween_move(img.id, 0, img.x, img.y)
			tween_rotate(img.id, 100, target2+180 )

			timerex(100, tween_move, 1, img.id, 150, x, y)
			timerex(100, tween_scale, 1, img.id, 150, 0, 3)

			leaves = leaves + 1
			
			local victim = cm.linecolision(x, y, target2, 1, dist)
			for index,vid in ipairs(victim) do
				if not cm.sameteam(vid,uid) then
					if not victim_pulled[vid] then
						victim_pulled[vid]=0
					end
					victim_pulled[vid]=victim_pulled[vid] + 1
					
				end	
			end
			
		end
	end
	
	for vid,damage in pairs(victim_pulled) do
		cm.customdamage(uid,vid,damage*15,"\169141208056Leaf Pull")
	end
	
	if leaves > 0 then
		return math.max(2,leaves*1)
	end	
	return 0
end

function cm.anchor(uid)
	local frame = 44
	local delay = 600
	local tiles = 12
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles,nil)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay+100,freeimage,1,s)
	tween_move(s,delay,mx,my)

	local damage_func = function(uid,vid,img)
		cm.bind(vid)
	
		local damage = math.random(40,60)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169100100100Anchor")
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		tween_move(img.id,0,img.x,img.y)
		tween_alpha(img.id,400,0)
		img.active = false
		
		cm.dash2(img.source,img.x,img.y)
		return true
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,100,100,100)
		
		tween_move(img.id,0,x,y)
		tween_alpha(img.id,400,0)
		img.active = false
		
		cm.dash2(img.source,x,y)
		return true
	end
	
	local tail_func = function(x,y,img)
		cm.effect("colorsmoke",x,y,1,1,100,100,100)
		return 3		
	end

	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,nil)
end

function cm.hook(uid)
	local frame = 55
	local delay = 600
	local tiles = 12
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay+100,freeimage,1,s)
	tween_move(s,delay,mx,my)

	local damage_func = function(uid,vid,img)
		local damage = math.random(30,40)	
		local x,y = cm.pos(vid)
		local ux,uy = cm.pos(uid)
		cm.customdamage(uid,vid,damage,"\169100100100Hook")
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		cm.dash2(vid,ux,uy)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,100,100,100)
	end
	
	local tail_func = function(x,y,img)
		cm.effect("colorsmoke",x,y,1,1,100,100,100)
		return 3		
	end

	cm.create_projectile(uid,s,damage_func,colision_func,tail_func,nil,nil)
end


function cm.blast(uid)
	local frame = 35
	local delay = 600
	local tiles = 13
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,1)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	imageblend(s,1)
	imagescale(s,0,1)
	
	tween_move(s,delay,mx,my)
	tween_scale(s,100,1,1)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(12,25)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169255150000Blast")
		cm.soundpos("weapons/flamethrower.ogg",x,y)
		cm.effect("fire",x,y,15,15,0,0,0)
		
		cm.burn(vid,uid)
	end
	
	local colision_func = function(x,y,img)
		cm.soundpos("weapons/flamethrower.ogg",x,y)
	
		cm.effect("fire",x,y,15,15,0,0,0)
	end
	
	local tail_func = function(x,y)
		cm.effect("fire",x,y,2,2,0,0,0)
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.meteor(uid)
	local frame = 43
	local delay = 2000
	local tiles = 20
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,1)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	--imageblend(s,1)
	--imagescale(s,0,1)
	
	tween_move(s,delay,mx,my)
	--tween_scale(s,delay/2,1,1)
	tween_rotateconstantly(s,5)
	
	timerex(delay,freeimage,1,s)
	
	local damage_func = function(uid,vid)
		local damage = math.random(12,25)	
		local x,y = cm.pos(vid)
		--cm.customdamage(uid,vid,damage,"Blast")
		--cm.soundpos("weapons/knife_hit.wav",x,y)
		
		--cm.effect("fire",x,y,15,15,0,0,0)
		cm.explosion(x,y,150,50,uid)
	end
	
	local colision_func = function(x,y,img)
		--cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,141,208,56)
		
		--cm.effect("fire",x,y,15,15,0,0,0)
		cm.explosion(x,y,150,50,img.source)
	end
	
	local tail_func = function(x,y)
		cm.effect("fire",x,y,2,2,0,0,0)
	end
	
	cm.create_projectile(uid,s,damage_func,colision_func,tail_func)
end

function cm.lovearrow(uid,tiles,offset)
	local tiles = tiles or 20
	local frame = 4
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles,offset)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(15,30)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169214040079Love Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
	end
	
	local colision_func = function(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,214,40,79)
		
		cm.particle(CM_SPRITESHEET_PATH,x,y,3,32,37)
	end
	
	local ally_func = function(uid,aid)
		local x,y = player(aid,"x"),player(aid,"y")
		cm.effect("flare",x,y,5,5,214,40,79)
		cm.particle(CM_SPRITESHEET_PATH,x,y,3,64,37)
		
		local hp = math.random(7,15)
		cm.sethealth(aid,player(aid,"health")+math.floor(hp))
		cm.healindicator(aid,uid,hp)
	end
	cm.create_projectile(uid,s,damage_func,colision_func,nil,ally_func)
end


function cm.piercingarrow(uid,dmg)
	local tiles = 20
	local frame = 4
	local delay = 800
	local x,y,rotation,mx,my = cm.angle(uid,32*tiles)
	local s = image(CM_SPRITESHEET_PATH,x,y,3)
	
	imageframe(s,frame)
	imagepos(s,x,y,rotation)
	imagescale(s,1.5,1.5)
	imagecolor(s,214,040,079)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	
	local damage_func = function(uid,vid)
		local damage = math.random(dmg-10,dmg+10)	
		local x,y = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"\169214040079Love Arrow")
		
		cm.soundpos("weapons/knife_hit.wav",x,y)
		
		return true
	end
	
	local ally_func = function(uid,aid)
		local x,y = player(aid,"x"),player(aid,"y")
		cm.effect("flare",x,y,5,5,214,40,79)
		cm.particle(CM_SPRITESHEET_PATH,x,y,3,64,37)
		
		local hp = math.random( math.floor(dmg/3), math.floor(dmg/2) )
		cm.sethealth(aid,player(aid,"health")+math.floor(hp))
		cm.healindicator(aid,uid,hp)
		
		return true
	end
	
	local tail_func = function(x,y)
		cm.effect("flare",x,y,1,1,214,040,079)
		return 5
	end
	cm.create_projectile(uid,s,damage_func,nil,tail_func,ally_func)
end

function cm.piercinglove(uid)
	local frame = 4
	local delay = 1500
	local s = image(CM_SPRITESHEET_PATH,1,0,uid+132)
	
	imageframe(s,frame)
	imagecolor(s,214,040,079)
	imagescale(s,1,1)
	--tween_rotateconstantly(s,-10)
	tween_scale(s,delay-500,1.5,1.5)
	timerex(delay,freeimage,1,s)
	
	local tail_func = function(x,y,img)
		local released = cm.isreleased(img.source,"E")
		if released then
			cm.effect("flare",x,y,5,16,214,040,079)
			cm.piercingarrow(uid,math.floor( 50 + released*30 ))
			
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.effect("flare",x,y,5,16,214,040,079)
		cm.piercingarrow(uid,80)
	end
	cm.create_projectile(uid,s,nil,nil,tail_func,nil,end_func)
end

function cm.throwmachete(uid)
	--local frame = 
	local delay = 1000
	local x,y,rotation,mx,my = cm.angle(uid,32*12)
	local s = image("gfx/weapons/machete.bmp<b>",x,y,3)
	
	imagepos(s,x,y,rotation)
	timerex(delay,freeimage,1,s)
	tween_move(s,delay,mx,my)
	tween_rotateconstantly(s,10)
	
	cm.strip(uid,69)
	
	local damage_func = function(uid,vid)
		local damage = math.random(70,80)
		local x,y,tx,ty = cm.pos(vid)
		cm.customdamage(uid,vid,damage,"Machete")
		cm.soundpos("weapons/knife_hit.wav",x,y)
		cm.spawnitem(69,tx,ty)
	end
	
	local colision_func = function(x,y)
		local tx,ty = cm.floor(x,y)
		cm.soundpos("weapons/knife_hitwall.wav",x,y)
		cm.effect("sparkles",x,y,5,5,210,230,255)
		cm.spawnitem(69,tx,ty)
	end
	
	local end_func = function(x,y)
		local tx,ty = cm.floor(x,y)
		cm.spawnitem(69,tx,ty)
	end
	cm.create_projectile(uid,s,damage_func,colision_func,nil,nil,end_func)
end

function cm.explosioncharge(uid)
	local delay = 1500
	local s = image("gfx/sprites/flare.png<b>",0,0,uid+132)
	
	imagecolor(s,255,128,0)
	imagescale(s,0.2,0.2)
	imageblend(s,1)
	tween_rotateconstantly(s,5)
	tween_scale(s,delay,1.5,1.5)
	timerex(delay,freeimage,1,s)
	
	local tail_func = function(x,y,img)
		local released = cm.isreleased(img.source,"E")
		if released then
			cm.explosion(x,y,150*(released*2),50*(released*2),uid)
			
			-- Disable projectile
			tween_alpha(img.id, 0, 0)
			img.active = false
		end
		return 3
	end
	
	local end_func = function(x,y,img)
		cm.explosion(x,y,450,75,uid)
	end
	cm.create_projectile(uid,s,nil,nil,tail_func,nil,end_func)
end
-----------------------------------------------------------------------------------------------------
-- End of class abilities
-----------------------------------------------------------------------------------------------------


function cm.healeffect(uid,amount)
	local x,y=cm.pos(uid) 
	cm.effect("flare",x,y,3,3,0,255,0) 
end

function cm.regen_wall(uid,x,y,tile_id)
	parse("settile "..x.." "..y.." "..tile_id)	
	for index,vid in ipairs(closeplayers(x*32+16,y*32+16,16)) do
		if uid~=vid then
			cm.customkill(uid,"Wallblock",vid) 
		end	
	end
end


function cm.leap(uid,step)
	local x,y,tx,ty,rot=cm.pos(uid)
	if rot <= 90 then 
		rot=rot+360 
	end 
	local angle=math.rad(math.abs(rot+90))-math.pi 
	
	
	local points = {}
	local px,py = x,y
	for i=1,step do
		px=px+math.cos(angle)*16
		py=py+math.sin(angle)*16
		
		if tile(math.floor(px/32),math.floor(py/32),"walkable") 
		and px>0 and py>0 and px<map("xsize")*32 and py<map("ysize")*32
		then
			points[i] = {px,py}
		else
			break
		end
	end
	
	
	for i=1,#points do
		timerex(i*20,cm.setpos,1,uid,points[i][1],points[i][2])
	end

	--timerex(#points*20,cm.earthquake,1,uid)
	--timerex(#points*20,cm.shake,1,uid,10)
	--timerex(#points*20,cm.leap_damage,1,uid)
end

function cm.leap_damage(uid)
	local damage = math.random(20,40)
	local radius = 128
	local x,y = player(uid,"x"),player(uid,"y")
	cm.customdamageaoe(uid,x,y,radius,damage,"Leap")
end

function cm.dash(uid,dist)
	--local x,y,tx,ty,rot = cm.pos(uid)
	local x,y,rotation,dx,dy = cm.angle(uid,dist)
	local point,status = cm.line(x,y,dx,dy,cm.linemap_px)
	local cooldown
	if status then
		local tp = point[math.min(6,#point)]
		cooldown=2
	else
		if #point > 0 then
			local tp = point[#point]
			cooldown=2
		else
			cooldown=0
		end
	end
	
	for i= 1,#point,8 do
		timerex( (i-1) /2,cm.setpos,1,uid,point[i][1],point[i][2])
	end
	return cooldown
end

function cm.dash2(uid,dx,dy)
	local x,y,tx,ty,rot = cm.pos(uid)
	local point,status = cm.line(x,y,dx,dy,cm.linemap_px)
	
	if status then
		local tp = point[math.min(6,#point)]
	else
		if #point > 0 then
			local tp = point[#point]
		end
	end
	
	for i=1,#point,8 do
		timerex(i*2,cm.setpos,1,uid,point[i][1],point[i][2])
	end
end

---------------------------------------------------------------------------------

function cm.create_projectile(uid,image_id,hit,colision,tail,ally,ending)
	local data = {
		id = image_id,
		victim = {},
		ally = {},
		object = {},
		damage = nil,
		weapon = nil,
		active = true,
		source = uid,
		onhit = hit,
		oncolision = colision, 
		calculation = true,
		ontail = tail,
		onend = ending,
		onhitally = ally,
		taildelay = 0,
		rot = object(image_id,"rootrot"),
		ox = imageparam(image_id,"x"),
		oy = imageparam(image_id,"y"),
		x = imageparam(image_id,"x"),
		y = imageparam(image_id,"y"),
		--group="projectile",
	}
	
	
	cm.image_projectile[image_id]=data
	--table.insert(cm.image_projectile,data)
end

function cm.projectile_update()
	for key,img in pairs(cm.image_projectile) do
		-- check if player is alive
		
	
		-- Check player colision
		if not imageparam(img.id,"x") then
			if img.onend and img.active then
				img.onend(img.x, img.y, img)
			end
			cm.image_projectile[key]=nil
		elseif img.active==true then
			if img.taildelay > 0 then
				img.taildelay = img.taildelay - 1
			end
			local x,y,rot
			if object(img.id,"mode") > 4 then
				x,y = player(img.source,"x"),player(img.source,"y")
			else
				x,y = object(img.id,"x"),object(img.id,"y")
			end	
			rot = object(img.id,"rot")
			
			
			if img.onhit or img.onhitally then
				--[[
				for index,oid in pairs(closeobjects(x,y,32)) do
					if not cm.image_projectile[oid] then
						if object(oid,"type") <= 15 then
							if img.onhit then
								img.onhit(img.source, oid + 32, img)
							else
								--cm.damageobject(oid,1,img.source)
							end
						end
					end	
				end--]]
			
			
				for index,tid in ipairs( closeplayers(x,y,16) ) do
					if tid ~= img.source
					and not img.victim[tid] 
					and not cm.sameteam(tid, img.source)
					and img.onhit
					then
						img.victim[tid]=true

						local status = img.onhit(img.source, tid, img)
						if not status then
							tween_alpha(img.id,0,0)
							img.active=false
						end
					end	
					
					if tid ~= img.source
					and not img.ally[tid] 
					and cm.sameteam(tid, img.source) 
					and img.onhitally
					then
						img.ally[tid]=true

						local status = img.onhitally(img.source, tid, img)
						if not status then
							tween_alpha(img.id,0,0)
							img.active=false
						end
					end	
				end	
			end
			
			if img.ontail and img.taildelay==0 then
				img.taildelay = img.ontail(img.x, img.y, img) or 3
			end
			
			if img.oncolision and img.calculation then
				local col_x,col_y,angle = cm.raytrace(img.x, img.y, x, y)
				
				if col_x then
					--function(uid,image_id, projectile_id)
					local status = img.oncolision(col_x, col_y, img, angle)
					if not status then
						tween_alpha(img.id,0,0)
						img.active=false
					end
				end
			end
			
			img.x, img.y, img.rot = x,y,rot
		end		
	end
end

function cm.projectile_dispose(projectile_id)
	
end

---------------------------------------------------------------------------------
function cm.create_timed_effect(uid,source,name,duration,data)
	local effect_data = {}
	local timer = cm.main_clock
	
	if type(data)=="string" then
	elseif type(data)=="table" then
		effect_data = data
		if effect_data.delay == nil then
			effect_data.delay = 0
		end
		if effect_data.name==nil then
			effect_data.name = "No name effect"
		end
	elseif data==nil then
		effect_data = {
			delay = 0,
			name = "No name effect",
			ending = function(uid,source,stack)
				print("ended")
			end,
			middle = function(uid,source,stack)
				print("occuring")
			end,
			stack = function(uid,source,stack)
				print("stacking")
			end,
		}
	end
	
	if effect_data.stack_count == nil then
		effect_data.stack_count = 1
	end	
	effect_data.name = name
	
	if cm.player_timers[uid][name] ~= nil then
		cm.player_timers[uid][name].stack_count = cm.player_timers[uid][name].stack_count + 1
		cm.player_timers[uid][name].source = source
		-- Multiply by the factor of 50 (CS2D FPS speed)
		cm.player_timers[uid][name].duration = duration*50 + timer
		if effect_data.stack then 
			effect_data.stack(uid,source,cm.player_timers[uid][name].stack_count)
		end
	else
		effect_data.source = source
		-- Multiply by the factor of 50 (CS2D FPS speed)
		effect_data.duration = duration*50 + timer
		cm.player_timers[uid][name] = effect_data
	end
end

function cm.check_timed_effect(uid,name)
	local timers = cm.player_timers[uid]
	for key,data in pairs(timers) do
		if data.name:lower()==name:lower() then
			return data
		end
	end
	return false
end

function cm.timed_effect_update()
	local timer = cm.main_clock
	for index,uid in pairs(player(0,"tableliving")) do
		local timers = cm.player_timers[uid]
		for key,data in pairs(timers) do
			if data.delay > 0 then
				data.delay = data.delay - 1
			end	
			
			if timer < data.duration then
				if data.middle and data.delay == 0 then
					data.delay = data.middle(uid, data.source, data.stack_count) or 20
				end
			else
				if data.ending then
					data.ending(uid)
				end
				cm.player_timers[uid][key]=nil
			end
		end
	end
end

function cm.dispose_timed_effect(uid,name)
	local data = cm.player_timers[uid][name]
	if data and data.ending then
		data.ending(uid, data.source)
	end
	cm.player_timers[uid][name]=nil
end


function cm.burn(uid,source)
	local effect_data = {
		middle = function(uid,source)
			local x,y = player(uid,"x"),player(uid,"y")
			local damage = math.random(1,5)
			cm.effect("fire",x,y,1,1,255,255,255)
			cm.customdamage(source,uid,damage,"\169255129000Burn")
			return 3
		end,
	}
	cm.create_timed_effect(uid,source,"burn",3,effect_data)
end

function cm.poison(uid,source,stack)
	local effect_data = {
		middle = function(uid,source,stack)
			local x,y = player(uid,"x"),player(uid,"y")
			local damage = math.min(stack,5)
			cm.effect("colorsmoke",x,y,damage,1,141,208,56)
			cm.customdamage(source,uid,damage,"\169141208056Poison")
			return 8
		end,
		stack_count = stack or 1,
	}
	cm.create_timed_effect(uid,source,"poison",3,effect_data)
end


function cm.bind(uid)
	local seconds = 2

	if cm.player_imgslot[uid][10] == nil then
		local bind_img = image(CM_SPRITESHEET_PATH,0,1,132+uid)
		imageframe(bind_img,42)
		imageblend(bind_img,1)
		imagealpha(bind_img,0.5)
		imagecolor(bind_img,255,0,255)
		
		tween_rotateconstantly(bind_img,5)
		cm.player_imgslot[uid][10] = bind_img
	end
	
	local speed = cm.classdata[cm.player[uid]].speed or 0
	cm.speedmod(uid, speed - 100)

	local effect_data = {
		ending = function(uid)
			local speed = cm.classdata[cm.player[uid]].speed or 0
			cm.speedmod(uid, speed )
			
			if cm.player_imgslot[uid][10] then
				freeimage(cm.player_imgslot[uid][10])
				
				cm.player_imgslot[uid][10] = nil
			end	
		end,
	}
	cm.create_timed_effect(uid,uid,"bind",seconds,effect_data)
end

function cm.slow2(uid)
	if cm.player_imgslot[uid][11] == nil then
		local bind_img = image(CM_SPRITESHEET_PATH,0,1,132+uid)
		imageframe(bind_img,42)
		imageblend(bind_img,1)
		imagealpha(bind_img,0.3)
		imagecolor(bind_img,255,255,0)
		
		tween_rotateconstantly(bind_img,5)
		cm.player_imgslot[uid][11] = bind_img
	end

	if not cm.check_timed_effect(uid,"bind") then
		local speed = cm.classdata[cm.player[uid]].speed or 0
		cm.speedmod(uid, speed - 14)
	end	
	
	
	local effect_data = {
		ending = function(uid)
			local speed = cm.classdata[cm.player[uid]].speed or 0
			cm.speedmod(uid, speed )
			
			if cm.player_imgslot[uid][11] then
				freeimage(cm.player_imgslot[uid][11])
				
				cm.player_imgslot[uid][11] = nil
			end	
		end,
	}
	cm.create_timed_effect(uid,uid,"slow",2,effect_data)
end

function cm.slow(uid)
	if not cm.check_timed_effect(uid,"bind") then
		local speed = cm.classdata[cm.player[uid]].speed or 0
		cm.speedmod(uid, speed - 14)
	end	
	
	local effect_data = {
		middle = function(uid)
			local x,y = player(uid,"x"),player(uid,"y")
			cm.effect("colorsmoke",x,y,1,1,255,255,255)
			return 3
		end,
		
		ending = function(uid)
			local speed = cm.classdata[cm.player[uid]].speed or 0
			cm.speedmod(uid, speed )
		end,
	}
	cm.create_timed_effect(uid,uid,"slow",3,effect_data)
end

function cm.speedbuff(uid)
	if not cm.check_timed_effect(uid,"bind") then
		local speed = cm.classdata[cm.player[uid]].speed or 0
		cm.speedmod(uid, speed + 20)
	end	
	
	local effect_data = {
		middle = function(uid)
			local x,y = player(uid,"x"),player(uid,"y")
			cm.effect("colorsmoke",x,y,1,1,0,0,0)
			return 1
		end,
		
		ending = function(uid)
			local speed = cm.classdata[cm.player[uid]].speed or 0
			cm.speedmod(uid, speed )
		end,
	}
	cm.create_timed_effect(uid,uid,"speed",3,effect_data)
end



---------------------------------------------------------------------------------
function cm.create_npc(image_id,hit,tail,behaviour,shape)
	local data = {
		id = image_id,
		victim = {},
		ally = {},
		damage = nil,
		weapon = nil,
		active = true,
		source = nil,
		onhit = hit,
		--oncolision = colision, 
		ontail = tail,
		taildelay = 0,
		x = imageparam(image_id,"x"),
		y = imageparam(image_id,"y"),
	}
	
	table.insert(cm.image_npc,data)
end

function cm.spawn_eye(x,y)
	local eye = image("gfx/cs2dorg/projectilewars/eye.png",0,0,3)
	
	imagepos(eye,x,y,0)
	tween_rotateconstantly(eye,1)
	
	local damage_func = function(vid,uid)
	end
	
	cm.create_npc(eye,damage_func)
end
--timerex(5000,cm.spawn_eye,1,500,500)
--cm.spawn_eye(0,0)

---------------------------------------------------------------------------------
-- Particle Effects
---------------------------------------------------------------------------------
function cm.blink(tid,r,g,b)
	local r = r or 255
	local g = g or 255
	local b = b or 255
	local flare=image("gfx/sprites/flare3.bmp",0,0,3)
	imageblend(flare,1)
	imagescale(flare,2,2)
	imagealpha(flare,0.8)
	imagecolor(flare,r,g,b)
	
	imagepos(flare,player(tid,"x"),player(tid,"y"),0)
	tween_alpha(flare,1000,0)
	timerex(1000,freeimage,1,flare)
end

function cm.blink2(x,y,r,g,b)
	local r = r or 255
	local g = g or 255
	local b = b or 255
	local flare=image("gfx/sprites/flare3.bmp",0,0,3)
	imageblend(flare,1)
	imagescale(flare,2,2)
	imagealpha(flare,0.8)
	imagecolor(flare,r,g,b)
	
	imagepos(flare,x,y,0)
	tween_alpha(flare,1000,0)
	timerex(1000,freeimage,1,flare)
end

function cm.particle(file,x,y,amount,spread,frame)
	local s = {}
	local frame=frame or 1
	for i=1,amount do 
		s[i]=image(file,0,0,3)
		imageblend(s[i],1)
		imagescale(s[i],1,1)
		imagealpha(s[i],0.8)
		imageframe(s[i],frame)
		--imagecolor(s[i],r,g,b)
		imagepos(s[i],x,y,0)
		local mx = math.random(x-spread,x+spread)
		local my = math.random(y-spread,y+spread)
		
		tween_alpha(s[i],500,0)
		tween_move(s[i],500,mx,my)
		timerex(500,freeimage,1,s[i])
	end
end


function cm.darkblink(x,y)
	local flare=image(CM_SPRITESHEET_PATH,x,y,3)
	imageframe(flare,83)
	imagescale(flare,0.5,0.5)
	imagecolor(flare,0,0,0)
	imagepos(flare,x,y,0)
	--imagealpha(flare,0.8)
	
	tween_scale(flare,1000,2,2)
	tween_alpha(flare,1000,0)
	timerex(1000,freeimage,1,flare)
end

function cm.afterimage(tid)
	local team = player(tid,"team")
	local look = player(tid,"look")+1
	local tbl = {[1]="t",[2]="ct",[3]="vip"}
	local str
	if team==3 then
		str = "vip"
	elseif team==2 or team==1 then
		str = tbl[team]..look
	end	
	
	
	local flare=image("<spritesheet:gfx/player/"..str..".bmp:32:32:b>",0,0,3)
	imageframe(flare,5)
	imageblend(flare,1)
	imagescale(flare,1,1)
	imagealpha(flare,0.8)
	imagecolor(flare,210,230,255)
	
	imagepos(flare,player(tid,"x"),player(tid,"y"),player(tid,"rot"))
	tween_alpha(flare,1000,0)
	timerex(1000,freeimage,1,flare)
end


function cm.damageindicator(vid,aid,amount,color)
	local color = color or "\169255255255"
	local sw,sh=player(aid,"screenw"),player(aid,"screenh")
	local vsw,vsh=player(vid,"screenw"),player(vid,"screenh")
	local x = sw/2 - player(aid,"x")+player(vid,"x")
	local y = sh/2 - player(aid,"y")+player(vid,"y")

	local align=1  
	local delay=1000
	
	
	-- Damage indicator for every individual ID.
	-- IDs used: 100, 101-132
	
	--local hudtxt_id =  100 + vid
	
	cm.hudtxt2(aid, 100 + vid, color.."-"..amount, x, y, align) 
	cm.hudtxtmove(aid, 100 + vid,delay, x, y-40) 
	cm.hudtxtalphafade(aid, 100 + vid, delay+500, 0) 

	cm.hudtxt2(vid, 100, color.."-"..amount, vsw/2, vsh/2, align) 
	cm.hudtxtmove(vid, 100, delay,vsw/2, (vsh/2)+40) 
	cm.hudtxtalphafade(vid, 100, delay+500, 0) 
end

function cm.customdamageaoe(uid,x,y,radius,damage,name,effect)
	local players = {}
	for index,vid in ipairs(closeplayers(x,y,radius)) do
		if vid~=uid and not cm.sameteam(uid,vid) then
			if effect and player(uid,"health")>0 then
				effect(vid)
			end
			
			if player(uid,"health")>0 then
				cm.customdamage(uid,vid,damage,name)
			end	
			
			if player(uid,"health")>0 then
				table.insert(players,vid)
			end	
		end	
	end
	for _,id in pairs(closeobjects(x,y,128)) do
		if object(id,"type") <= 15 then
			cm.damageobject(id,math.floor(damage/2),uid)
		end
	end	
	
	return players
end

function cm.customdamage(uid,vid,damage,name)
	-- CUSTOMDAMAGE FUNC
	if vid > 32 then
		local oid = uid - 32 
		cm.damageobject(oid,damage,uid)
		return 
	end

	local rawdmg = damage
	local armorlist={
		--[200]=57
		[201]=79;
		[202]=80;
		[203]=81;
		[204]=82;
		[205]=83;
		[206]=84;
	}
	local armor=player(vid,"armor")
	local armor_id
	if armor==0 then
		armor_id=""
	elseif armor > 0 and armor <= 65 then
		armor_id="57"
	elseif armor > 65 and armor <= 100 then	
		armor_id="58"
	elseif armor > 100 and armor <= 200 then
		armor_id="58"
	else
		armor_id=armorlist[armor]
	end
	
	if armor_id==79 then -- 25	
		damage=math.ceil(damage*0.75)
	elseif armor_id==80 then --50
		damage=math.ceil(damage*0.50)
	elseif armor_id==81 then -- 75
		damage=math.ceil(damage*0.25)
	elseif armor_id==82 then -- 50
		damage=math.ceil(damage*0.50)
	elseif armor_id==83 then --95
		damage=math.ceil(damage*0.05)
	end
	
	local color
	if name:sub(1,1) == "\169" then
		color = name:sub(1,10)
		name = name:sub(11)
	end

	local status = cm.hit(vid,uid,256,damage,damage,rawdmg,0,color)
	if status==0 or status==nil then
		if player(vid,"health") <= damage then
			cm.customkill(uid,name,vid) 
		else
			cm.sethealth(vid,player(vid,"health") - damage)
			cm.sound2(vid,"player/hit"..math.random(1,3)..".wav")
		end
	else
		-- Do nothing
	end
	--cm.damageindicator(vid,uid,damage)
end

function cm.healindicator(uid,source,amount)
	cm.sethealth(uid,player(uid,"health")+amount)
	
	local x,y,tx,ty,rot = cm.pos(uid)
	local sw,sh=player(uid,"screenw"),player(uid,"screenh")
	local align=1  
	local delay=1000	
	
	
	-- Healing indicator for every individual ID.
	-- hudtxt IDs used: 133-164 (132 + uid)
	-- self healing: 165
	
	if source~=0 and source~=uid then
		local sx,sy,stx,sty,srot = cm.pos(source)
		local ssw,ssh = player(source,"screenw"),player(source,"screenh")
		
		local ix = ssw/2 - (sx - x)
		local iy = ssh/2 - (sy - y)
		
		cm.hudtxt2(source,132 + uid,"\169000255000+"..amount,ix,iy,align) 
		cm.hudtxtalphafade(source,132 + uid,delay+500,0) 
		cm.hudtxtmove(source,132 + uid,delay,ix,iy-40) 
	end	
	cm.hudtxt2(uid,165,"\169000255000+"..amount,sw/2,sh/2,align) 
	cm.hudtxtalphafade(uid,165,delay+500,0) 
	cm.hudtxtmove(uid,165,delay,sw/2,(sh/2)+40) 
end

function cm.popup(uid,text,id,x,y,align,delay,direction) 
	
	local id=id or 0
	local screenw,screenh=player(uid,"screenw"),player(uid,"screenh")
	local x=x or screenw/2
	local y=y or screenh/2
	local align=align or 1  
	local delay=delay or 1000
	
	cm.hudtxt2(uid,id,text,x,y,align) 
	cm.hudtxtalphafade(uid,id,delay+1000,0) 
	
	if direction=="up" or direction==nil then
		cm.hudtxtmove(uid,id,delay,x,y-40) 
	elseif direction=="down" then
		cm.hudtxtmove(uid,id,delay,x,y+40) 
	elseif direction=="left" then
		cm.hudtxtmove(uid,id,delay,x-40,y) 
	elseif direction=="right" then
		cm.hudtxtmove(uid,id,delay,x+40,y) 
	end
end 

function cm.objectamount(uid,type)
	local counter=0
	for index,oid in ipairs(object(0,"table")) do
		if object(oid,"player") ==  uid and object(oid,"type") == type then
			counter=counter+1
		end
	end
	return counter
end

function cm.buildclear(uid)
	for _,id in ipairs(object(0,"table")) do
		if object(id,"player") == uid then
			parse("killobject "..id)
		end
	end	
end

function cm.tilearea(x,y,size)
	local tx,ty=math.floor(x/32),math.floor(y/32)
	local t={}
	if size==1 then 
		t[1]={tx,ty}
	elseif size==2 then	
		t = {
			{tx+0,ty-1};
			{tx-1,ty+0};
			{tx+0,ty+0};
			{tx+0,ty+1};
			{tx+1,ty+0};
		}
	elseif size==3 then
		t = {
			-- Corners
			{tx-1,ty+1};
			{tx-1,ty-1};
			{tx+1,ty-1};
			{tx+1,ty+1};
			-- Cross
			{tx+0,ty-1};
			{tx-1,ty+0};
			{tx+0,ty+0};
			{tx+0,ty+1};
			{tx+1,ty+0};
		}
	elseif size==4 then
		-- Corners
		t = {
			{tx-1,ty+1};
			{tx-1,ty-1};
			{tx+1,ty-1};
			{tx+1,ty+1};
			-- Cross
			{tx+0,ty-1};
			{tx-1,ty+0};
			{tx+0,ty+0};
			{tx+0,ty+1};
			{tx+1,ty+0};
			-- Outer
			{tx+2,ty+0};
			{tx+0,ty+2};
			{tx-2,ty+0};
			{tx+0,ty-2};
		}
	end

	return t
end

function cm.autoattack_update()
	for index,uid in pairs(player(0,"tableliving")) do
		if cm.attackdelay[uid]>0 then
			cm.attackdelay[uid] = cm.attackdelay[uid] - 1
		end
	
		if not menu.active(uid) then
			local key = "mouse1"
			local cid = cm.player[uid]
			local status = cm.player_input[uid][key]
			local action
				
			if status==1 then
				action = cm.classdata[cid].onautoattack
			end
					
			if action ~= nil and cm.attackdelay[uid]==0 then
				cm.attackdelay[uid] = cm.classdata[cid].attackdelay or 50
				action(uid)
			end
		end
	end
end


function cm.key_input_update()
	for index,uid in pairs(player(0,"tableliving")) do
	if not menu.active(uid) then
	
	
		for key,status in pairs(cm.player_input[uid]) do
			if cm.player_cooldown[uid][key] then
				local cid = cm.player[uid]
				local status = cm.player_input[uid][key]
				local action
				
				if status==1 then
					if key=="E" then
						action=cm.classdata[cid].onabilitypressed
					elseif key=="Q" then
						action=cm.classdata[cid].onultimatepressed
					elseif key=="leftshift" then
						action=cm.classdata[cid].onshiftpressed
					elseif key=="mouse2" then
						action=cm.classdata[cid].onrightclickpressed
					elseif key=="mouse1" then
						action=cm.classdata[cid].onleftclickpressed
					end
				end
				
				if action~=nil then
					action(uid)
				end
			end
			
		end	
	end	
	end
end

function cm.key_input(uid,key,state)
	if not player(uid,"exists") or player(uid,"health")<=0 or menu.active(uid) then return end
	
	if cm.player_cooldown[uid][key] then
		if state==1 then -- Pressed
			local cid = cm.player[uid] -- Class ID
			local cooldown = cm.player_cooldown[uid][key] -- Key cooldown
			local action
			
			if key=="E" then
				action=cm.classdata[cid].onability
			elseif key=="Q" then
				action=cm.classdata[cid].onultimate
			elseif key=="leftshift" then
				action=cm.classdata[cid].onshift
			elseif key=="mouse2" then
				action=cm.classdata[cid].onrightclick
			elseif key=="mouse1" then
				action=cm.classdata[cid].onleftclick
			end
			
			if action ~= nil  then
				if cooldown < cm.main_clock then
					local cooldown_a, err = action(uid)
					
					if CM_NO_COOLDOWN then
						cooldown_a = 0
					end
					
					-- Multiplying by the FPS of CS2D.
					cooldown_a = math.floor(cooldown_a*50)
					
					if cooldown_a > 0 then
						cm.player_cooldown[uid][key] = cooldown_a + cm.main_clock
						
						-- updates the hud ski1l readiness
						cm.update_cooldown(uid)
						
						-- update the hud skill readiness after x seconds
						timerex(cooldown_a*21,cm.update_cooldown,1,uid)
						
						-- sfx
						cm.sound2(uid,"player/build.wav")
					elseif cooldown_a == 0 then
						-- Nothing.
					else
						-- popup for cooldown message
						-- hudtxt IDs used: 90
						cm.popup(uid,err,90)
					end
				else
					-- Dividing by the factor of CS2D FPS frame.
					local cd_label = (cooldown - cm.main_clock) / 50
					cm.popup(uid,"Cooldown: ".. string.format("%.1f",cd_label).."!",90)
				end			
			end
			
		elseif state==0 then -- Released
			local cid = cm.player[uid] -- Class ID
			local cooldown = cm.player_cooldown[uid][key] -- Key cooldown
			local action
			
			local time_passed = (cm.main_clock - cm.player_input_timer[uid][key]) or 0
			
			
			if key=="E" then
				action=cm.classdata[cid].onabilityreleased
			elseif key=="Q" then
				action=cm.classdata[cid].onultimatereleased
			elseif key=="leftshift" then
				action=cm.classdata[cid].onshiftreleased
			elseif key=="mouse2" then
				action=cm.classdata[cid].onrightclickreleased
			elseif key=="mouse1" then
				action=cm.classdata[cid].onleftclickreleased
			end
			
			if action ~= nil then
				if cooldown < cm.main_clock then
					local cooldown_a,err = action(uid,time_passed)
					if CM_NO_COOLDOWN then
						cooldown_a = 0
					end
					
					-- Multiplying by the FPS of CS2D.
					cooldown_a = math.floor(cooldown_a*50)
					
					if cooldown_a > 0 then
						cm.player_cooldown[uid][key] = cooldown_a + cm.main_clock
						
						-- updates the hud ski1l readiness
						cm.update_cooldown(uid)
						
						-- update the hud skill readiness after x seconds
						timerex(cooldown_a*21,cm.update_cooldown,1,uid)
						
						-- sfx
						cm.sound2(uid,"player/build.wav")
					elseif cooldown_a == 0 then
						-- Nothing.
					else
						cm.popup(uid,err,90)
					end
				else
					-- Dividing by the factor of CS2D FPS frame.
					local cd_label = (cooldown - cm.main_clock) / 50
					cm.popup(uid,"Cooldown: ".. string.format("%.1f",cd_label).."!",90)
				end	
			end
		end
	end	
end

function cm.getcooldown(uid,key)
	local cooldown =  (cm.player_cooldown[uid][key] - cm.main_clock)/50
	local cooldown_s = string.format("%.1f",cooldown)
	return cooldown_s
end

function cm.ispressed(uid,key)
	if cm.player_input[uid][key]==1 then
		return true
	end
	return false
end

function cm.isreleased(uid,key)
	if cm.player_input[uid][key]==0 then
		-- return the value in seconds
		return ( cm.main_clock - cm.player_input_timer[uid][key] )/50
	end
	return false
end

-----------------------------------------------------------------------------------------------
addhook("spawn","cm.spawn")
addhook("key","cm.key")
addhook("join","cm.join")
addhook("leave","cm.leave")
addhook("startround_prespawn","cm.startround")
addhook("second","cm.second")
addhook("attack","cm.attack")
addhook("hit","cm.hit")
addhook("die","cm.die")
addhook("move","cm.move")
addhook("movetile","cm.movetile")
addhook("buy","cm.buy")
addhook("drop","cm.drop")
addhook("walkover","cm.walkover")
addhook("ms100","cm.ms100")
addhook("always","cm.always")
addhook("buildattempt","cm.buildattempt")
addhook("hitzone","cm.hitzone")

--- Hooks do C-4 para desativar.
freehook("buildattempt","menu.fastbuild");
freehook("hit","menu.hit");
freehook("spawn","menu.spawn");

addbind("Q")
addbind("E")
addbind("leftshift")
addbind("space")
addbind("mouse2")
addbind("mouse1")

-----------------------------------------------------------------------------------------------

function cm.hitzone(image_id, player_id, object_id, weapon, impact_x, impact_y, damage)
	--cm.effect("colorsmoke",impact_x,impact_y,20,20,255,255,255)
	cm.effect("sparkles",impact_x,impact_y,5,1,200,200,255)
	
	local uid = cm.clone_ref[ image_id ]
	if uid ~= nil and uid~=0 then
		local weapon_img = cm.clone[uid].weapon_img
		freeimage(weapon_img)
		imagehitzone(image_id,0)
		freeimage(image_id)
		cm.clone[uid].img = 0 
		cm.clone[uid].weapon_img = 0 
	end	
end


function cm.buildattempt(uid,type,x,y)
	local cid = cm.classdata[cm.player[uid]]
	local status,err
	if cid.onbuild then
		status,err=cid.onbuild(uid,type,x,y)
	end	
	return status
end

function cm.ms100()
	--profile.start()

	cm.timed_effect_update()
	cm.key_input_update()
	
	
	for index,uid in pairs(player(0,"tableliving")) do
		local c = cm.classdata[cm.player[uid]]
		
		if c.onupdate then
			c.onupdate(uid)
		end
	end
	
	--profile.stop()
end

function cm.always()
	cm.main_clock = cm.main_clock + 1
	
	
	--profile.start()
	
	cm.autoattack_update()
	cm.projectile_update()
	
	--profile.stop()
end


function cm.second()
	for _,uid in pairs(player(0,"table")) do
		local cid = cm.player[uid]
		local c = cm.classdata[cid]
		if player(uid,"health")>0 then
			if c.inf then
				cm.equip(uid,c.inf)
			end
			if c.onsecond ~= nil then
				c.onsecond(uid)
			end
			if c.regen then
				cm.sethealth(uid,player(uid,"health")+c.regen)
			end
		end	
		
		if player(uid,"bot") then
			local target = ai_findtarget(uid)
			local x,y = cm.pos(target)
			if player(target,"exists") and ai_freeline(uid,x,y) then
				cm.player_input[uid]["mouse1"]=1
			else
				cm.player_input[uid]["mouse1"]=0
			end
		end	
	end
end

function cm.buy(uid)
	return 1
end

function cm.move(uid,x,y)
	local cid = cm.classdata[cm.player[uid]]
	if cid.onmove then
		cid.onmove(uid,x,y)
	end
end

function cm.movetile(uid,x,y)
	local cid = cm.classdata[cm.player[uid]]
	if cid.onmovetile then
		cid.onmovetile(uid,x,y)
	end
	 
	cm.player_lastposition[uid][1] = x
	cm.player_lastposition[uid][2] = y 
end

function cm.drop(uid,item_id,item_type)
	local cid = cm.classdata[cm.player[uid]]
	
	if cid.candrop then
		return 0
	elseif item_type==70 or item_type==71 then
		return 0
	end
	return 1
end

function cm.walkover(uid,item_id,item_type)
	local cid = cm.classdata[cm.player[uid]]
	
	if cid.onpick then
		cid.onpick(uid,item_id,item_type)
	end

	local allowed = {55,63,64,65,66,67,68,70,71,61,62}
	for k,v in ipairs(allowed) do
		if v == item_type then
			return 0
		end	
	end
	
	
	if cid.canpick then
		if cid.picklist then
			for k,v in ipairs(cid.picklist) do
				if v==item_type then
					return 0
				end	
			end
		else
			return 0
		end	
	end
	return 1
end

function cm.hit(vid,aid,weapon,hpdmg,apdmg,rawdmg,object,color)
	if vid==aid then
		return 1
	end	
	
	if cm.player_spawnprotection[vid] > cm.main_clock then
		--return 1
	end

	if aid == 0 then
		-- Damage indicator for hurting yourself
		-- hudtxt ID used: 166
	
		cm.popup(vid,"\169255000000-"..hpdmg,166,nil,nil,nil,nil,"down") 
		return 0
	end

	--local color = color or nil

	cid_attacker = cm.classdata[cm.player[aid]]
	cid_victim = cm.classdata[cm.player[vid]]
	
	if cid_attacker.onhit then
		cid_attacker.onhit(vid,aid,weapon,hpdmg,apdmg,rawdmg,object)
	end
	
	if cid_victim.ondamagetaken then
		local callback = cid_victim.ondamagetaken(vid,aid,weapon,hpdmg,apdmg,rawdmg,object)
		
		if callback==false then
			return 1
		end
	end

	if not cm.sameteam(vid,aid) then
		cm.damageindicator(vid,aid,hpdmg,color)
	end
end

function cm.attack(uid)
	cid = cm.classdata[cm.player[uid]]
	if cid.onattack then
		cid.onattack(uid)
	end
	
	if player(uid,"bot") then
		--[[
		if cid.onautoattack then
			cid.onautoattack(uid)
		end
		--]]
		
		cm.key_input(uid,"Q",1)
		cm.key_input(uid,"E",1)
	end
end

function cm.update_cooldown(uid)
	local c = cm.getclass(uid)
	if c.onability then
		local cooldown = cm.player_cooldown[uid]["E"] - cm.main_clock
		if cooldown > 0 then
			cm.update_cooldown_hud(uid,"E","\169255000000not ready!")
		else
			cm.update_cooldown_hud(uid,"E","ready!")
		end
	else
		cm.update_cooldown_hud(uid,"E","")
	end
	
	if c.onultimate then
		local cooldown = cm.player_cooldown[uid]["Q"] - cm.main_clock
		if cooldown > 0 then
			cm.update_cooldown_hud(uid,"Q","\169255000000not ready!")
		else
			cm.update_cooldown_hud(uid,"Q","ready!")
		end
	else
		cm.update_cooldown_hud(uid,"Q","")
	end
end


function cm.update_cooldown_hud(uid,key,flag)

	-- Interface used to show cooldown to player
	-- hudtxt IDs used: 80-89
	local label
	if flag=="" then
		label=""
	else
		label = "\169255255255"..key:upper()..": "..flag
	end
	
	local sw,sh = player(uid,"screenw"),player(uid,"screenh")
	if key=="E" then
		cm.hudtxt2(uid, 80, label, sw/2 + 60, sh - 100, 0, 1, 20)
	elseif key=="Q"  then
		cm.hudtxt2(uid, 81, label, sw/2 - 60, sh - 100, 2, 1, 20)
	end

	--]]
	--[[
	elseif key=="leftshift" then
		cm.hudtxt2(uid,82,"S: "..label,cm.hud_position_x,cm.hud_position_y+40,0,0,20)
	elseif key=="mouse2" then
		cm.hudtxt2(uid,83,"R: "..label,cm.hud_position_x,cm.hud_position_y+60,0,0,20)
	elseif key=="mouse1" then
		cm.hudtxt2(uid,84,"L: "..label,cm.hud_position_x,cm.hud_position_y+80,0,0,20)
	end
	--]]
end

function cm.die(vid,kid,weapon,x,y,killerobject)
		
	cm.setmoney(kid,0)
	cm.setmoney(vid,0)

	local images = cm.player_imgslot[vid]
	for key,value in pairs(images) do
		if value ~= 0 then 
			freeimage(value)
		end
	end
	
	local timers = cm.player_timers[vid]
	for key,data in pairs(timers) do
		if data.ending then
			data.ending(vid)
		end	
	end

	cm.player_imgslot[vid]={}
	cm.player_timers[vid]={}
	cm.player_extra[vid]={}
	cm.player_trace[vid]={}
	
	
	local cid = cm.player[kid] or 0
	local cid_v = cm.player[vid] or 0
	local c = cm.classdata[cid]
	local cv = cm.classdata[cid_v]
	
	if c and c.onkill and kid ~= 0 then
		c.onkill(kid,weapon,vid)
	end
	
	if cv and cv.ondie then
		cv.ondie(vid)
	end
	
	-- Triumph rune
	cm.sethealth(kid,player(kid,"health")+10)
	cm.healindicator(kid,kid,10)
	
	
	cm.player_dieposition[vid][1] = cm.player_lastposition[vid][1]
	cm.player_dieposition[vid][2] = cm.player_lastposition[vid][2]
	
	--[[
	if not cm.winrate[cid] then
		cm.winrate[cid]=1
	else
		cm.winrate[cid]=cm.winrate[cid]+1
	end
	
	if not cm.winrate[cid_v] then
		cm.winrate[cid_v]=1
	else
		cm.winrate[cid_v]=cm.winrate[cid_v]+1
	end
	--]]
	return 1
end

function cm.reset(uid)
	if cm.playerskin[uid] then
		freeimage(cm.playerskin[uid])
	end	
	
	if cm.clone[uid].img ~= 0 then
		freeimage(cm.clone[uid].img)
		freeimage(cm.clone[uid].weapon_img)
		cm.clone_ref[ cm.clone[uid].img ] = nil
		cm.clone[uid].img = 0 
		cm.clone[uid].weapon_img = 0 
	end
	
	for k,value in ipairs(cm.player_imgslot[uid]) do
		if value ~= 0 then 
			freeimage(value)
		end
	end
	
	local timers = cm.player_timers[uid]
	for key, data in pairs(timers) do
		if data.ending then
			data.ending(uid)
		end	
	end

	cm.player_imgslot[uid]={}
	cm.player_timers[uid]={}
	cm.player_extra[uid]={}
	cm.player_trace[uid]={}	
	cm.player[uid]=0
	cm.playercd[uid]=0
	cm.playerskin[uid]=0
	
	cm.buildclear(uid)
	
	cm.speedmod(uid,0)
	cm.setmaxhealth(uid,100)
	cm.setmoney(uid,0)
end

function cm.join(uid)
	cm.reset(uid)
end

function cm.leave(uid)
	cm.reset(uid)
end

function cm.startround()
	--[[
	for uid=1,32 do
		cm.reset(uid)
	end
	--]]
end


function cm.spawn(uid)
	cm.player_spawnprotection[uid] = cm.main_clock + 3*50

	local rand=math.random(1,#cm.classdata)
	if cm.player_select[uid]~=0 then
		rand = cm.player_select[uid]
	end
	cm.reset(uid)
	--[[
	if player(uid,"bot") then
		rand = 1
	end--]]
	
	cm.player[uid]=rand
	local c=cm.classdata[rand]
	
	if c.onspawn then
		c.onspawn(uid)
	end
	
	if c.speed then
		cm.speedmod(uid,c.speed)
	end	
	if c.weapon then
		cm.setweapon(uid,c.weapon)
	end	
	
	if c.melee then
		if c.melee=="claw" then
			timerex(40,cm.equip,1,uid,78)
			timerex(40,cm.setweapon,1,uid,78)
		elseif c.melee=="machete" then
			timerex(40,cm.equip,1,uid,69)
			timerex(40,cm.setweapon,1,uid,69)
		elseif c.melee=="chainsaw" then
			timerex(40,cm.equip,1,uid,85)
			timerex(40,cm.setweapon,1,uid,85)
		elseif c.melee=="wrench" then
			timerex(40,cm.equip,1,uid,74)
			timerex(40,cm.setweapon,1,uid,74)
		elseif c.melee=="hand" then
			timerex(40,cm.equip,1,uid,75)
			timerex(40,cm.setweapon,1,uid,75)			
			timerex(40,cm.setammo,1,uid,75,0,0)
		end
		timerex(50,cm.strip,1,uid,50)
	end
	
	if c.noknife then
		timerex(50,cm.strip,1,uid,50)
	end	
	
	if c.health then
		cm.setmaxhealth(uid,c.health)
	else
		cm.setmaxhealth(uid,100)
	end
	
	if c.skin then
		cm.playerskin[uid]=image(c.skin, 3, 0, 200 + uid)
		if c.skinframe then
			imageframe(cm.playerskin[uid],c.skinframe)
		end	
		
		if c.skinscale then
			imagescale(cm.playerskin[uid],c.skinscale,c.skinscale)
		end
		
		if c.skincolor then
			imagecolor(cm.playerskin[uid],c.skincolor[1],c.skincolor[2],c.skincolor[3])
		end	
	end	
	
	local equips = c.equips or "X"
	
	local t=cm.split(equips,",")
	local e=""
	for k,v in ipairs(t) do
		if cm.cs2ditemdata[string.upper(v)] then
			e=e..","..cm.cs2ditemdata[string.upper(v)]
		else
			e=e..","..v
		end
	end

	msg2(uid,"\169255255255You are: "..cm.classdata[rand].name)
	if c.desc then
		msg2(uid,"\169255255000"..c.desc)
	end
	
	-- Description of abilities
	-- hudtxt IDs used: 85-89
	-- Interface of abilities
	cm.update_cooldown(uid)
	
	local sw,sh = player(uid,"screenw"),player(uid,"screenh")
	if c.abilitydesc then
		cm.hudtxt2(uid, 85, "\169255255255" .. (c.abilitydesc), sw/2 + 60, sh - 80, 0, 1, 16)
	else
		cm.hudtxt2(uid, 85, "")
	end	

	if c.ultimatedesc then
		cm.hudtxt2(uid, 86, "\169255255255" .. (c.ultimatedesc), sw/2 - 60, sh - 80, 2, 1, 16)
	else
		cm.hudtxt2(uid, 86, "")
	end			
	return e
end

function cm.key(uid,key,state)
	cm.player_input[uid][key]=state
	
	if state==1 then
		cm.player_input_timer[uid][key] = cm.main_clock
	elseif state==0 then
	end	
	
	cm.key_input(uid,key,state)
end

-----------------------------------------------------------------------------------------------------
function cm.hero_name(id)
	if id==0 then 
		return "Random"
	else
		if cm.classdata[id] then
			return cm.classdata[id].name
		end		
	end
end


function cm.hero_select(uid,cid)
	if cm.classdata[cid] then
		local x,y = player(uid,"x"),player(uid,"y")
		cm.player_select[uid]=cid
		
		local health = player(uid,"health")
		parse('spawnplayer '..uid..' -1 -1')
		parse('setpos '..uid..' '..x..' '..y)
		
		cm.sethealth(uid,health)
	else
		msg2(uid,"Hero "..cid.." does not exist")
	end	
end

function cm.hero_list(uid)
	local newdata={
		title="HERO LIST";
		button={
			{
				name="Random",
				caption="Random",
				func="cm.hero_select("..uid..",0)",
			},
		},
	}
	
	for cid = 1,#cm.classdata do
		newdata.button[#newdata.button+1]={
			name=cm.classdata[cid].name,
			caption=cm.classdata[cid].category,
			func="cm.hero_select("..uid..","..cid..")";
		}
	end
	menu.show(uid,newdata)
end

-----------------------------------------------------------------------------------------------------
menu.data["@main_menu"]["button"][6] = {
	caption="${cm.hero_name(cm.player_select[v_me])}",
	name="SELECT HERO",
	target="$cm.hero_list(v_me)",
}




--[[
if LUA_MENU then

	menu.data["@main_menu"]["button"][4].condition=nil
	menu.data["@main_menu"]["button"][4].caption=nil

	menu.addbutton("@main_menu",{ 
		name="SELECT A HERO",
		caption="${cm.hero_name(cm.player_select[v_me])}",
		target="$cm.hero_list(v_me)",
	})
	
end

if LUA_ADMIN then
	admin.action["@winrate"]={
		timer=10,
		group={"admin"},
		args={},
		hide=false,
		syntax="@winrate",
		event = function(uid)
			for cid,value in pairs(cm.winrate) do
				local classname = cm.classdata[cid].name

				msg2(uid,classname..": "..value)
			end
			return true
		end,
	}
end
--]]