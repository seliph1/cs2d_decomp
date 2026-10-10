-------------------------------------------------------------------
-- Wrapper functions for easy usage of CS2D commands in Lua      --
-- 25.03.2011 - www.UnrealSoftware.de                            --
-------------------------------------------------------------------

-------------------------------------------------------------------
-- These functions make it easier to use CS2D commands in Lua    --
-- They call the parse functions with the fitting parameters     --
--                                                               --
-- Example without wrapper function:                             --
-- parse("spawnplayer "..id.." "..x.." " ..y)                    --
--                                                               --
-- Example with wrapper function:                                --
-- spawnplayer(id,x,y)                                           --
--                                                               --
-- Both examples do exactly the same but using the wrapper       --
-- function is much easier and not as error-prone as using parse.--
-- Therefore it is recommended to use the wrapper functions.     --
--                                                               --
-- Use the following line to include the wrapper in your script: --
-- if wrapper~=true then dofile("sys/lua/wrapper.lua") end       --
-------------------------------------------------------------------

-- Set Wrapper state to LOADED/TRUE!
-- This flag can be used to check if the wrapper has already been loaded
wrapper=true

-- Functions
function bot_add()
	parse("bot_add")
end

function bot_add_ct()
	parse("bot_add_ct")
end

function bot_add_t()
	parse("bot_add_t")
end

function bot_count(count)
	parse("bot_count "..count)
end

function bot_jointeam(team)
	parse("bot_jointeam "..team)
end

function bot_kill()
	parse("bot_kill")
end

function bot_prefix(prefix)
	parse("bot_prefix "..prefix)
end

function bot_remove()
	parse("bot_remove")
end

function bot_remove_all()
	parse("bot_remove_all")
end

function bot_remove_ct()
	parse("bot_remove_ct")
end

function bot_remove_t()
	parse("bot_remove_t")
end

function bot_skill(skill)
	parse("bot_skill "..skill)
end

function bot_weapons(weapons)
	parse("bot_weapons "..weapons)
end

function changelevel(map)
	parse("changelevel "..map)
end

function changemap(map)
	parse("changemap "..map)
end

function customkill(killer,weaponname,victim)
	parse("customkill "..killer.." "..weaponname.." "..victim)
end

function deathslap(player)
	parse("deathslap "..player)
end

function effect(effect,x,y,p1,p2,r,g,b)
	parse('effect "'..effect..'" '..x..' '..y..' '..p1..' '..p2..' '..r..' '..g..' '..b)
end

function equip(player,weapon)
	parse("equip "..player.." "..weapon)
end

function explosion(x,y,size,damage,sourceplayer)
	parse("explosion "..x.." "..y.." "..size.." "..damage.." "..sourceplayer)
end

function flashplayer(player,intensity)
	parse("flashplayer "..player.." "..intensity)
end

function flashposition(x,y,intensity)
	parse("flashposition "..x.." "..y.." "..intensity)
end

function hudtxt(id,text,x,y,align)
	parse("hudtxt "..id.." "..text.." "..x.." "..y.." "..align)
end

function hudtxt2(player,id,text,x,y,align)
	parse("hudtxt2 "..player.." "..id.." "..text.." "..x.." "..y.." "..align)
end

function hudtxtalphafade(player,id,duration,alpha)
	parse("hudtxtalphafade "..player.." "..id.." "..duration.." "..alpha)
end

function hudtxtcolorfade(player,id,duration,red,green,blue)
	parse("hudtxtcolorfade "..player.." "..id.." "..duration.." "..red.." "..green.." "..blue)
end

function hudtxtmove(player,id,duration,x,y)
	parse("hudtxtmove "..player.." "..id.." "..duration.." "..x.." "..y)
end

function items()
	parse("items")
end

function kick(player,reason)
	parse("kick "..player.." "..reason)
end

function killobject(objectid)
	parse("killobject "..objectid)
end

function killplayer(player)
	parse("killplayer "..player)
end

function listbans()
	parse("listbans")
end

function listplayers()
	parse("listplayers")
end

function logaddress_add(address)
	parse("logaddress_add "..address)
end

function logaddress_remove(address)
	parse("logaddress_remove "..address)
end

function logaddress_removeall()
	parse("logaddress_removeall")
end

function lua(script)
	parse("lua "..script)
end

function luareset()
	parse("luareset")
end

function makect(player)
	parse("makect "..player)
end

function makespec(player)
	parse("makespec "..player)
end

function maket(player)
	parse("maket "..player)
end

-- Map() function shouldn't be overwritten by default
-- function map(map)
-- 	parse("map "..map)
-- end

function maps()
	parse("maps")
end

function mp_antispeeder(antispeederprotection)
	parse("mp_antispeeder "..antispeederprotection)
end

function mp_autogamemode(autogamemode)
	parse("mp_autogamemode "..autogamemode)
end

function mp_autoteambalance(balance)
	parse("mp_autoteambalance "..balance)
end

function mp_building_health(building,price)
	parse("mp_building_health "..building.." "..price)
end

function mp_building_limit(building,limit)
	parse("mp_building_limit "..building.." "..limit)
end

function mp_building_price(building,price)
	parse("mp_building_price "..building.." "..price)
end

function mp_buytime(_time)
	parse("mp_buytime ".._time)
end

function mp_c4timer(_time)
	parse("mp_c4timer ".._time)
end

function mp_curtailedexplosions(curtailed)
	parse("mp_curtailedexplosions "..curtailed)
end

function mp_damagefactor(factor)
	parse("mp_damagefactor "..factor)
end

function mp_deathdrop(drop)
	parse("mp_deathdrop "..drop)
end

function mp_dispenser_health(health)
	parse("mp_dispenser_health "..health)
end

function mp_dispenser_money(money)
	parse("mp_dispenser_money "..money)
end

function mp_dmspawnmoney(money)
	parse("mp_dmspawnmoney "..money)
end

function mp_dropgrenades(drop)
	parse("mp_dropgrenades "..drop)
end

function mp_floodprot(floodprotection)
	parse("mp_floodprot "..floodprotection)
end

function mp_freezetime(_time)
	parse("mp_freezetime ".._time)
end

function mp_grenaderebuy(allowed)
	parse("mp_grenaderebuy "..allowed)
end

function mp_hostagepenalty(kills)
	parse("mp_hostagepenalty "..kills)
end

function mp_idleaction(action)
	parse("mp_idleaction "..action)
end

function mp_idlekick(idleaction)
	parse("mp_idlekick "..idleaction)
end

function mp_idletime(_time)
	parse("mp_idletime ".._time)
end

function mp_infammo(infiniteammo)
	parse("mp_infammo "..infiniteammo)
end

function mp_kevlar(percent)
	parse("mp_kevlar "..percent)
end

function mp_kickpercent(percent)
	parse("mp_kickpercent "..percent)
end

function mp_killbuildingmoney(money)
	parse("mp_killbuildingmoney "..money)
end

function mp_killbuildings(kill)
	parse("mp_killbuildings "..kill)
end

function mp_killinfo(info)
	parse("mp_killinfo "..info)
end

function mp_killteambuildings(mode)
	parse("mp_killteambuildings "..mode)
end

function mp_lagcompensation(maxcompensationtime)
	parse("mp_lagcompensation "..maxcompensationtime)
end

function mp_localrconoutput(output)
	parse("mp_localrconoutput "..output)
end

function mp_luamap(use)
	parse("mp_luamap "..use)
end

function mp_luaserver(file)
	parse("mp_luaserver "..file)
end

function mp_mapgoalscore(score)
	parse("mp_mapgoalscore "..score)
end

function mp_mapvoteratio(percent)
	parse("mp_mapvoteratio "..percent)
end

function mp_maxclientsip(clients)
	parse("mp_maxclientsip "..clients)
end

function mp_maxrconfails(maxfails)
	parse("mp_maxrconfails "..maxfails)
end

function mp_natholepunching(allowpunching)
	parse("mp_natholepunching "..allowpunching)
end

function mp_pinglimit(limit)
	parse("mp_pinglimit "..limit)
end

function mp_radar(radar)
	parse("mp_radar "..radar)
end

function mp_randomspawn(_random)
	parse("mp_randomspawn ".._random)
end

function mp_reservations(reservations)
	parse("mp_reservations "..reservations)
end

function mp_respawndelay(delay)
	parse("mp_respawndelay "..delay)
end

function mp_roundlimit(rounds)
	parse("mp_roundlimit "..rounds)
end

function mp_roundtime(_time)
	parse("mp_roundtime ".._time)
end

function mp_startmoney(money)
	parse("mp_startmoney "..money)
end

function mp_supply_items(itemlist)
	parse("mp_supply_items "..itemlist)
end

function mp_teamkillpenalty(kills)
	parse("mp_teamkillpenalty "..kills)
end

function mp_teleportreload(reloadtime)
	parse("mp_teleportreload "..reloadtime)
end

function mp_tempbantime(_time)
	parse("mp_tempbantime ".._time)
end

function mp_timelimit(_time)
	parse("mp_timelimit ".._time)
end

function mp_tkpunish(kill)
	parse("mp_tkpunish "..kill)
end

function mp_trace(accuracy)
	parse("mp_trace ".."accuracy")
end

function mp_turretdamage(damage)
	parse("mp_turretdamage "..damage)
end

function mp_unbuildable(buildings)
	parse("mp_unbuildable "..buildings)
end

function mp_unbuyable(unbuyable)
	parse("mp_unbuyable "..unbuyable)
end

function mp_vulnerablehostages(vulnerable)
	parse("mp_vulnerablehostages "..vulnerable)
end

function mp_winlimit(wins)
	parse("mp_winlimit "..wins)
end

function mp_wpndmg(name,damage)
	parse("mp_wpndmg "..name.." "..damage)
end

function mp_wpndmg_z1(name,damage)
	parse("mp_wpndmg_z1 "..name.." "..damage)
end

function mp_wpndmg_z2(name,damage)
	parse("mp_wpndmg_z2 "..name.." "..damage)
end

function mp_zombiedmg(percentageofdamage)
	parse("mp_zombiedmg "..percentageofdamage)
end

function mp_zombiekillequip(weapon)
	parse("mp_zombiekillequip "..weapon)
end

function mp_zombiekillscore(killscore)
	parse("mp_zombiekillscore "..killscore)
end

function mp_zombierecover(recover)
	parse("mp_zombierecover "..recover)
end

function mp_zombiespeedmod(speedmod)
	parse("mp_zombiespeedmod "..speedmod)
end

function msg(message)
	parse("msg "..message)
end

function rcon(commands)
	parse("rcon "..commands)
end

function rcon_password(password)
	parse("rcon_password "..password)
end

function rcon_pw(password)
	parse("rcon_pw "..password)
end

function removeitem(item)
	parse("removeitem "..item)
end

function reroute(player,address)
	parse("reroute "..player.." "..address)
end

function restart(delay)
	parse("restart "..delay)
end

function restartround(delay)
	parse("restartround "..delay)
end

function setarmor(player,armor)
	parse("setarmor "..player.." "..armor)
end

function setdeaths(player,deaths)
	parse("setdeaths "..player.." "..deaths)
end

function sethealth(player,health)
	parse("sethealth "..player.." "..health)
end

function setmaxhealth(player,maxhealth)
	parse("setmaxhealth "..player.." "..maxhealth)
end

function setmoney(player,money)
	parse("setmoney "..player.." "..money)
end

function setname(player,name)
	parse("setname "..player.." "..name)
end

function setpos(player,x,y)
	parse("setpos "..player.." "..x.." "..y)
end

function setscore(player,score)
	parse("setscore "..player.." "..score)
end

function setweapon(player,weapon)
	parse("setweapon "..player.." "..weapon)
end

function shake(player,power)
	parse("shake "..player.." "..power)
end

function slap(player)
	parse("slap "..player)
end

function spawnitem(item,x,y)
	parse("spawnitem "..item.." "..x.." "..y)
end

function spawnnpc(_type,x,y,rot)
	parse("spawnnpc ".._type.." "..x.." "..y.." "..rot)
end

function spawnobject(_type,x,y,rot,mode,team,player)
	parse("spawnobject ".._type.." "..x.." "..y.." "..rot.." "..mode.." "..team.." "..player)
end

function spawnplayer(player,x,y)
	parse("spawnplayer "..player.." "..x.." "..y)
end

function speedmod(player,speedmod)
	parse("speedmod "..player.." "..speedmod)
end

function strip(player,weapon)
	parse("strip "..player.." "..weapon)
end

function sv_fow(fow)
	parse("sv_fow "..fow)
end

function sv_friendlyfire(FF)
	parse("sv_friendlyfire "..FF)
end

function sv_gamemode(mode)
	parse("sv_gamemode "..mode)
end

function sv_gm(mode)
	parse("sv_gm "..mode)
end

function sv_hostport(port)
	parse("sv_hostport "..port)
end

function sv_lan(lan)
	parse("sv_lan "..lan)
end

function sv_map(map)
	parse("sv_map "..map)
end

function sv_maptransfer(transfer)
	parse("sv_maptransfer "..transfer)
end

function sv_maxplayers(players)
	parse("sv_maxplayers "..players)
end

function sv_msg(message)
	parse("sv_msg "..message)
end

function sv_msg2(player,message)
	parse("sv_msg2 "..player.." "..message)
end

function sv_name(name)
	parse("sv_name "..name)
end

function sv_password(password)
	parse("sv_password "..password)
end

function sv_rcon(rconpassword)
	parse("sv_rcon "..rconpassword)
end

function sv_restart(delay)
	parse("sv_restart "..delay)
end

function sv_restartround(delay)
	parse("sv_restartround "..delay)
end

function sv_sound(soundfile)
	parse("sv_sound "..soundfile)
end

function sv_sound2(player,soundfile)
	parse("sv_sound2 "..player.." "..soundfile)
end

function sv_specmode(mode)
	parse("sv_specmode "..mode)
end

function sv_spraytransfer(spraytransfer)
	parse("sv_spraytransfer "..spraytransfer)
end

function sv_usgnonly(usgnonly)
	parse("sv_usgnonly "..usgnonly)
end

function transfer_speed(speed)
	parse("transfer_speed "..speed)
end

function trigger(trigger)
	parse("trigger "..trigger)
end

function unban(banmask)
	parse("unban "..banmask)
end

function unbanall()
	parse("unbanall")
end

function usgn_addserver()
	parse("usgn_addserver")
end

function usgn_info()
	parse("usgn_info")
end