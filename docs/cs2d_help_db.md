# CS2D Help & Reference Database (Scraped from cs2d.com)

## console_commands (https://www.cs2d.com/help.php?cat=all)

```text
Commands/LuaReference for console commands, Lua hooks and Lua commands in CS2D.

	Console Commands are entered in console, executed with key-binds, cfg-files or with Lua parse

	Lua Hooks execute Lua when events occur - some have a return value which can change the game behavior

	Lua Commands are used to retrieve game data or to perform actions in Lua scripts

  CS2D Console Commands

	ALL

	Script

	Controls

	Settings

	Server

	Bots

	Debug

	Security

Category: all (375)ah
ak47
alias
aliases
attack
attack2
aug
autobuy
autoscreen
autoswitch
awp
back
banip
banname
bans
bansteam
banusgn
bind
borderless
bot
bot_add
bot_add_ct
bot_add_t
bot_autofill
bot_count
bot_freeze
bot_jointeam
bot_keepfreeslots
bot_kill
bot_prefix
bot_remove
bot_remove_all
bot_remove_ct
bot_remove_t
bot_skill
bot_weapons
bullets
buy
buyammo1
buyammo2
buyequip
changelevel
changemap
chooseteam
clear
clientport
close
cmsg
connections
console
controller
copyaddress
customkill
damageobject
damagetile
deagle
deathnoticeimg
deathslap
debug
debugai
debuglog
debuglua
debugstats
defuser
disconnect
dmgindicator
download
drawradar
drop
echo
effect
elite
elites
endround
equip
exec
exit
explosion
famas
flare
flash
flashlight
flashplayer
flashposition
fn57
forward
fowalpha
fowdetails
fps
freeprojectile
g3sg1
galil
glock
gore
gpad_deadzone
gpad_firezone
grenademode
hegren
hudtxt
hudtxt2
hudtxtalphafade
hudtxtclear
hudtxtcolorfade
hudtxtmove
hud_fastswitch
hud_takesshots
include
invnext
invprev
items
itemshadows
kick
kill
killobject
killplayer
lastinv
left
lefthand
listbans
listplayers
localport
logaddress_add
logaddress_remove
logaddress_removeall
logbatch
logmaxflushrate
logsplit
logstamp
lua
luareset
m249
m3
m4a1
mac10
makect
makespec
maket
map
mapinfo
maps
mapshadows
mapsounds
markplayer
memory
menuselect
menu_lanscanport
mp5
mp_antispeeder
mp_autogamemode
mp_autoteambalance
mp_building_health
mp_building_limit
mp_building_price
mp_buymenu
mp_buytime
mp_c4timer
mp_connectionlimit
mp_curtailedexplosions
mp_damagefactor
mp_deathdrop
mp_dispenser_health
mp_dispenser_money
mp_dmspawnmoney
mp_dropgrenades
mp_flashlight
mp_floodprot
mp_floodprotignoretime
mp_freezetime
mp_grenaderebuy
mp_hostagepenalty
mp_hovertext
mp_hud
mp_hudscale
mp_idleaction
mp_idlekick
mp_idletime
mp_infammo
mp_kevlar
mp_kickpercent
mp_killbuildingmoney
mp_killbuildings
mp_killinfo
mp_killteambuildings
mp_lagcompensation
mp_lagcompensationdivisor
mp_localrconoutput
mp_luamap
mp_luaserver
mp_mapgoalscore
mp_mapvoteratio
mp_maxclientsip
mp_maxrconfails
mp_natholepunching
mp_pinglimit
mp_postspawn
mp_radar
mp_randomspawn
mp_recoil
mp_reservations
mp_respawndelay
mp_roundlimit
mp_roundtime
mp_shotweakening
mp_smokeblock
mp_startmoney
mp_supply_items
mp_teamkillpenalty
mp_teleportreload
mp_tempbantime
mp_timelimit
mp_tkpunish
mp_trace
mp_turretdamage
mp_unbuildable
mp_unbuyable
mp_vulnerablehostages
mp_weaponfadeout
mp_winlimit
mp_wpndmg
mp_wpndmg_z1
mp_wpndmg_z2
mp_zombiedmg
mp_zombiekillequip
mp_zombiekillscore
mp_zombierecover
mp_zombiespeedmod
msg
name
netinfo
nick
nightvision
nvgs
p228
p90
particles
playershadows
quit
radio1
radio2
radio3
rcon
rcon_password
rcon_pw
rebuy
recoil
relativemovement
reload
removeitem
render3d
renderdecals
renderdecoration
reroute
resources
restart
restartround
right
say
sayteam
scout
screenshot
server
serveraction1
serveraction2
serveraction3
serverinfo
setammo
setarmor
setassists
setdeaths
sethealth
setmaxhealth
setmoney
setmvp
setname
setpos
setscore
setteamscores
settile
setweapon
SG550
sg552
sgren
shadows
shadowshade_obstacle
shadowshade_wall
shadowz_obstacles
shadowz_wall
shadow_angle
shadow_frameskipping
shake
shield
showscores
slap
slot0
slot1
slot10
slot2
slot3
slot4
slot5
slot6
slot7
slot8
slot9
snapshot
sounddriver
soundmode
spawnitem
spawnnpc
spawnobject
spawnplayer
spawnprojectile
specmode
specplayer
speed
speedmod
spray
spraycolor
spraylogo
strip
sv_checkusgnlogin
sv_daylighttime
sv_forcelight
sv_fow
sv_friendlyfire
sv_gamemode
sv_gm
sv_hostport
sv_lan
sv_map
sv_maptransfer
sv_maxplayers
sv_msg
sv_msg2
sv_name
sv_offscreendamage
sv_password
sv_rcon
sv_rconusers
sv_restart
sv_restartround
sv_sound
sv_sound2
sv_soundpos
sv_specmode
sv_spraytransfer
sv_stopsound
sv_usgnonly
team
tileanimations
tmp
transfer_speed
trigger
triggerposition
ump45
unban
unbanall
unbindall
use
usgn_addserver
usgn_info
usgn_pw
usgn_user
usp
vest
vesthelm
visibleshots
volume
vote
votemap
votemenu
weapon
wiggle
win
xm1014
  Lua Hooks

	ALL

	Basic

	Time

	Player

	Object

  Lua Commands

	ALL

	Basic

	Timer

	Image

	Game

	Map

	Tile

	Player

	Item

	Entity

	Object

	Projectile

	AI
```

---

## lua_hooks (https://www.cs2d.com/help.php?hookcat=all)

```text
Commands/LuaReference for console commands, Lua hooks and Lua commands in CS2D.

	Console Commands are entered in console, executed with key-binds, cfg-files or with Lua parse

	Lua Hooks execute Lua when events occur - some have a return value which can change the game behavior

	Lua Commands are used to retrieve game data or to perform actions in Lua scripts

  CS2D Console Commands

	ALL

	Script

	Controls

	Settings

	Server

	Bots

	Debug

	Security

  Lua Hooks

	ALL

	Basic

	Time

	Player

	Object

Category: all (79)always
assist
attack
attack2
bombdefuse
bombexplode
bombplant
break
build
buildattempt
buy
clientdata
clientsetting
collect
connect
connect_attempt
connect_initplayer
die
disconnect
dominate
drop
endround
flagcapture
flagtake
flashlight
hit
hitzone
hostagedamage
hostagekill
hostagerescue
hostageuse
httpdata
itemfadeout
join
key
kill
leave
log
mapchange
menu
minute
move
movetile
ms100
name
objectdamage
objectkill
objectupgrade
parse
projectile
projectile_impact
radio
rcon
reload
say
sayteam
sayteamutf8
sayutf8
second
select
serveraction
shieldhit
shutdown
spawn
specswitch
spray
startround
startround_prespawn
suicide
team
trigger
triggerentity
turretscan
use
usebutton
vipescape
voice
vote
walkover
  Lua Commands

	ALL

	Basic

	Timer

	Image

	Game

	Map

	Tile

	Player

	Item

	Entity

	Object

	Projectile

	AI
```

---

## lua_commands (https://www.cs2d.com/help.php?luacat=all)

```text
Commands/LuaReference for console commands, Lua hooks and Lua commands in CS2D.

	Console Commands are entered in console, executed with key-binds, cfg-files or with Lua parse

	Lua Hooks execute Lua when events occur - some have a return value which can change the game behavior

	Lua Commands are used to retrieve game data or to perform actions in Lua scripts

  CS2D Console Commands

	ALL

	Script

	Controls

	Settings

	Server

	Bots

	Debug

	Security

  Lua Hooks

	ALL

	Basic

	Time

	Player

	Object

  Lua Commands

	ALL

	Basic

	Timer

	Image

	Game

	Map

	Tile

	Player

	Item

	Entity

	Object

	Projectile

	AI

Category: all (88)addbind
addhook
ai_aim
ai_attack
ai_build
ai_buy
ai_debug
ai_drop
ai_findtarget
ai_freeline
ai_goto
ai_iattack
ai_move
ai_radio
ai_reload
ai_respawn
ai_rotate
ai_say
ai_sayteam
ai_selectweapon
ai_spray
ai_use
checksumfile
checksumstring
closehostage
closeitems
closeobjects
closeplayers
entity
entitylist
fow_in
freehook
freeimage
freeline
freerect
freetimer
funcs
game
hascloseplayers
hostage
image
imagealpha
imageblend
imagecolor
imageframe
imagehitzone
imageparam
imagepos
imagescale
inentityzone
item
itemtype
map
menu
msg
msg2
object
objectat
objecttype
parse
player
playerammo
playerweapons
print
projectile
projectilelist
randomentity
randomhostage
removeallbinds
removebind
reqcld
reqhttp
setentityaistate
sethookstate
stats
steamstats
tile
tileproperty
timer
tween_alpha
tween_animate
tween_color
tween_frame
tween_move
tween_rotate
tween_rotateconstantly
tween_scale
vars
```

---

## entity_lua (https://www.cs2d.com/help.php?luacat=entity&luacmd=entity)

```text
Commands/LuaReference for console commands, Lua hooks and Lua commands in CS2D.

	Console Commands are entered in console, executed with key-binds, cfg-files or with Lua parse

	Lua Hooks execute Lua when events occur - some have a return value which can change the game behavior

	Lua Commands are used to retrieve game data or to perform actions in Lua scripts

  CS2D Console Commands

	ALL

	Script

	Controls

	Settings

	Server

	Bots

	Debug

	Security

  Lua Hooks

	ALL

	Basic

	Time

	Player

	Object

  Lua Commands

	ALL

	Basic

	Timer

	Image

	Game

	Map

	Tile

	Player

	Item

	Entity

	Object

	Projectile

	AI

Category: entity (5)entity
entitylist
inentityzone
randomentity
setentityaistate
  entityCategories EntityParameters x 
 y 
 "value" 
InfoReturns a value of an entity at the tile position (X|Y):

exists: boolean, true if there is an entity at this position, false otherwise

typename: name of that entity type

type: internal type ID for that entity

name: name entered in the name field of that entity

trigger: value(s) entered in the trigger field of that entity

state: boolean, false if visible/on (default), true otherwise (true/false is not used as expected here! Attention!)

int0 - int9: internal integer for settings/states

str0 - str9: internal string for settings/states

aistate: AI state for AI scripting, can be set with "setentityaistate"

Note: int0-9 and str0-9 are used for entity settings/states and are different for each entity type! Most entity types do not use all of these variables!

Note: There is no entity(0,"table") as it is common for comparable commands! Use the command "entitylist" instead!

Attention: Env_Decal entities are NOT accessible with this command.
	
	
		var disqus_config = function () {
		this.page.url = 'http://www.cs2d.com/help.php?luacmd=entity';
		this.page.identifier = 'help-luacmd-entity';
		this.page.title='CS2D Lua Command: entity';
	};
	(function() {
		var d = document, s = d.createElement('script');
		s.src = 'https://cs2d.disqus.com/embed.js';
		s.setAttribute('data-timestamp', +new Date());
		(d.head || d.body).appendChild(s);
	})();
	
	Please enable JavaScript to view the comments powered by Disqus.
```

---

## weapons (https://www.cs2d.com/weapons.php)

```text
Weapons & Items           
    
        This is a list of all weapons and items in CS2D!
    

            
    
        ALL

        Primary

        Secondary

        Melee

        Grenades

        Explosives

        Other

    

Category: ALL (72)1 - USP
2 - Glock
3 - Deagle
4 - P228
5 - Elite
6 - Five-Seven
10 - M3
11 - XM1014
20 - MP5
21 - TMP
22 - P90
23 - Mac 10
24 - UMP45
30 - AK-47
31 - SG552
32 - M4A1
33 - Aug
34 - Scout
35 - AWP
36 - G3SG1
37 - SG550
38 - Galil
39 - Famas
40 - M249
41 - Tactical Shield
45 - Laser
46 - Flamethrower
47 - RPG Launcher
48 - Rocket Launcher
49 - Grenade Launcher
50 - Knife
51 - HE
52 - Flashbang
53 - Smoke Grenade
54 - Flare
55 - Bomb
56 - Defuse Kit
57 - Kevlar
58 - Kevlar+Helm
59 - Night Vision
60 - Gas Mask
61 - Primary Ammo
62 - Secondary Ammo
63 - Planted Bomb
64 - Medikit
65 - Bandage
66 - Coins
67 - Money
68 - Gold
69 - Machete
70 - Red Flag
71 - Blue Flag
72 - Gas Grenade
73 - Molotov Cocktail
74 - Wrench
75 - Snowball
76 - Air Strike
77 - Mine
78 - Claw
79 - Light Armor
80 - Armor
81 - Heavy Armor
82 - Medic Armor
83 - Super Armor
84 - Stealth Suit
85 - Chainsaw
86 - Gut Bomb
87 - Laser Mine
88 - Portal Gun
89 - Satchel Charge
90 - M134
91 - FN F2000
 1 - USPCategorySecondary (Slot: 2, Sub: 0)
Price500$
Damage24
Ammo12 | 100
Rate of Fire166 RPM (FD: 9)
RangeMedium (300)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s120 (5 x 24)
DPS Ø133.333 (5.556 x 24)
 2 - GlockCategorySecondary (Slot: 2, Sub: 0)
Price400$
Damage21
Ammo20 | 120
Rate of Fire166 RPM (FD: 9)
RangeMedium (250)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s105 (5 x 21)
DPS Ø116.667 (5.556 x 21)
 3 - DeagleCategorySecondary (Slot: 2, Sub: 0)
Price650$
Damage34
Ammo7 | 35
Rate of Fire100 RPM (FD: 15)
RangeMedium (300)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s102 (3 x 34)
DPS Ø113.333 (3.333 x 34)
 4 - P228CategorySecondary (Slot: 2, Sub: 0)
Price600$
Damage22
Ammo13 | 53
Rate of Fire166 RPM (FD: 9)
RangeMedium (300)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s110 (5 x 22)
DPS Ø122.222 (5.556 x 22)
 5 - EliteCategorySecondary (Slot: 2, Sub: 0)
Price1000$
Damage22
Ammo15 | 120
Rate of Fire187 RPM (FD: 8)
RangeMedium (300)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s132 (6 x 22)
DPS Ø137.5 (6.25 x 22)
 6 - Five-SevenCategorySecondary (Slot: 2, Sub: 0)
Price750$
Damage21
Ammo20 | 100
Rate of Fire214 RPM (FD: 7)
RangeMedium (300)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s147 (7 x 21)
DPS Ø150 (7.143 x 21)
 10 - M3CategoryPrimary (Slot: 1, Sub: 0)
Price1700$
Damage26 * 5 = 130 max
Ammo8 | 32
Rate of Fire75 RPM (FD: 20)
RangeShort (50)
WeightNormal (SpdM: 0)
AccuracyHorrible (V: 10)
DPS 1s260 (2 x 130)
DPS Ø325 (2.5 x 130)
 11 - XM1014CategoryPrimary (Slot: 1, Sub: 0)
Price3000$
Damage22 * 5 = 110 max
Ammo7 | 32
Rate of Fire125 RPM (FD: 12)
RangeShort (50)
WeightNormal (SpdM: 0)
AccuracyHorrible (V: 10)
DPS 1s440 (4 x 110)
DPS Ø458.333 (4.167 x 110)
 20 - MP5CategoryPrimary (Slot: 1, Sub: 0)
Price1500$
Damage13
Ammo30 | 120
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 2)
DPS 1s208 (16 x 13)
DPS Ø216.667 (16.667 x 13)
 21 - TMPCategoryPrimary (Slot: 1, Sub: 0)
Price1250$
Damage9
Ammo30 | 120
Rate of Fire750 RPM (FD: 2)
RangeShort (100)
WeightLight (SpdM: 0.35)
AccuracyBad (V: 4)
DPS 1s225 (25 x 9)
DPS Ø225 (25 x 9)
 22 - P90CategoryPrimary (Slot: 1, Sub: 0)
Price2350$
Damage11
Ammo50 | 100
Rate of Fire750 RPM (FD: 2)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 3)
DPS 1s275 (25 x 11)
DPS Ø275 (25 x 11)
 23 - Mac 10CategoryPrimary (Slot: 1, Sub: 0)
Price1400$
Damage9
Ammo30 | 90
Rate of Fire750 RPM (FD: 2)
RangeShort (100)
WeightLight (SpdM: 0.35)
AccuracyBad (V: 4)
DPS 1s225 (25 x 9)
DPS Ø225 (25 x 9)
 24 - UMP45CategoryPrimary (Slot: 1, Sub: 0)
Price1700$
Damage15
Ammo25 | 90
Rate of Fire375 RPM (FD: 4)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 3)
DPS 1s180 (12 x 15)
DPS Ø187.5 (12.5 x 15)
 30 - AK-47CategoryPrimary (Slot: 1, Sub: 0)
Price2500$
Damage22
Ammo30 | 90
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 3)
DPS 1s352 (16 x 22)
DPS Ø366.667 (16.667 x 22)
 31 - SG552CategoryPrimary (Slot: 1, Sub: 0)
Price3500$
Damage24 (Zoom 1: 35)
Ammo30 | 90
Rate of Fire375 RPM (FD: 4)
RangeFar (325)
WeightNormal (SpdM: 0)
AccuracyPerfect (V: 0)
DPS 1s288 (12 x 24)
DPS Ø300 (12.5 x 24)
 32 - M4A1CategoryPrimary (Slot: 1, Sub: 0)
Price3100$
Damage22
Ammo30 | 90
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 2)
DPS 1s352 (16 x 22)
DPS Ø366.667 (16.667 x 22)
 33 - AugCategoryPrimary (Slot: 1, Sub: 0)
Price3500$
Damage24 (Zoom 1: 35)
Ammo30 | 90
Rate of Fire375 RPM (FD: 4)
RangeFar (325)
WeightNormal (SpdM: 0)
AccuracyPerfect (V: 0)
DPS 1s288 (12 x 24)
DPS Ø300 (12.5 x 24)
 34 - ScoutCategoryPrimary (Slot: 1, Sub: 0)
Price2750$
Damage45 (Zoom 1: 50, Zoom 2: 100)
Ammo10 | 60
Rate of Fire75 RPM (FD: 20)
RangeFar (350)
WeightLight (SpdM: 0.1)
AccuracyPerfect (V: 0)
DPS 1s90 (2 x 45)
DPS Ø112.5 (2.5 x 45)
 35 - AWPCategoryPrimary (Slot: 1, Sub: 0)
Price4750$
Damage50 (Zoom 1: 65, Zoom 2: 150)
Ammo10 | 30
Rate of Fire23 RPM (FD: 65)
RangeFar (400)
WeightHeavy (SpdM: -0.5)
AccuracyPerfect (V: 0)
DPS 1s50 (1 x 50)
DPS Ø38.462 (0.769 x 50)
 36 - G3SG1CategoryPrimary (Slot: 1, Sub: 0)
Price5000$
Damage32 (Zoom 1: 37, Zoom 2: 45)
Ammo20 | 60
Rate of Fire250 RPM (FD: 6)
RangeFar (350)
WeightHeavy (SpdM: -0.3)
AccuracyGood (V: 1)
DPS 1s256 (8 x 32)
DPS Ø266.667 (8.333 x 32)
 37 - SG550CategoryPrimary (Slot: 1, Sub: 0)
Price4200$
Damage30 (Zoom 1: 35, Zoom 2: 40)
Ammo30 | 90
Rate of Fire250 RPM (FD: 6)
RangeFar (350)
WeightHeavy (SpdM: -0.3)
AccuracyGood (V: 1)
DPS 1s240 (8 x 30)
DPS Ø250 (8.333 x 30)
 38 - GalilCategoryPrimary (Slot: 1, Sub: 0)
Price2000$
Damage13
Ammo35 | 90
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyGood (V: 1)
DPS 1s208 (16 x 13)
DPS Ø216.667 (16.667 x 13)
 39 - FamasCategoryPrimary (Slot: 1, Sub: 0)
Price2250$
Damage14
Ammo25 | 90
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyGood (V: 1)
DPS 1s224 (16 x 14)
DPS Ø233.333 (16.667 x 14)
 40 - M249CategoryPrimary (Slot: 1, Sub: 0)
Price5750$
Damage15
Ammo100 | 200
Rate of Fire750 RPM (FD: 2)
RangeMedium (300)
WeightHeavy (SpdM: -0.5)
AccuracyHorrible (V: 8)
DPS 1s375 (25 x 15)
DPS Ø375 (25 x 15)
 41 - Tactical ShieldCategoryPrimary (Slot: 1, Sub: 0)
Price1000$
Damage0
Ammo0 | 0
Rate of Fire0 (0 frame(s) delay)
Range Very Short (0)
WeightHeavy (SpdM: -0.2)
AccuracyPerfect (V: 0)
 45 - LaserCategoryPrimary (Slot: 1, Sub: 0)
Price6500$
Damage1000
Ammo15 | 45
Rate of Fire50 RPM (FD: 30)
RangeFar (1000)
WeightHeavy (SpdM: -0.3)
AccuracyPerfect (V: 0)
DPS 1s1000 (1 x 1000)
DPS Ø1666.667 (1.667 x 1000)
 46 - FlamethrowerCategoryPrimary (Slot: 1, Sub: 0)
Price3200$
Damage30
Ammo100 | 200
Rate of Fire500 RPM (FD: 3)
RangeShort (55)
WeightHeavy (SpdM: -0.1)
AccuracyPerfect (V: 0)
DPS 1s480 (16 x 30)
DPS Ø500 (16.667 x 30)
 47 - RPG LauncherCategoryPrimary (Slot: 1, Sub: 0)
Price4000$
Damage325
Ammo1 | 2
Rate of Fire30 RPM (FD: 50)
RangeFar (700)
WeightHeavy (SpdM: -0.1)
AccuracyPerfect (V: 0)
DPS 1s325 (1 x 325)
DPS Ø325 (1 x 325)
 48 - Rocket LauncherCategoryPrimary (Slot: 1, Sub: 0)
Price5500$
Damage70
Ammo20 | 60
Rate of Fire75 RPM (FD: 20)
RangeFar (700)
WeightHeavy (SpdM: -0.1)
AccuracyPerfect (V: 0)
DPS 1s140 (2 x 70)
DPS Ø175 (2.5 x 70)
 49 - Grenade LauncherCategoryPrimary (Slot: 1, Sub: 0)
Price5500$
Damage75
Ammo20 | 60
Rate of Fire75 RPM (FD: 20)
RangeFar (320)
WeightHeavy (SpdM: -0.1)
AccuracyPerfect (V: 0)
DPS 1s150 (2 x 75)
DPS Ø187.5 (2.5 x 75)
 50 - KnifeCategoryMelee (Slot: 3, Sub: 0)
Price0$ (not buyable)
Damage45 (Secondary: 90)
Ammo0 | 0
Rate of Fire187 RPM (FD: 8)
Range Very Short (7)
WeightLight (SpdM: 0.75)
AccuracyPerfect (V: 0)
DPS 1s270 (6 x 45)
DPS Ø281.25 (6.25 x 45)
 51 - HECategoryGrenades (Slot: 4, Sub: 0)
Price300$
Damage105
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s525 (5 x 105)
DPS Ø525 (5 x 105)
 52 - FlashbangCategoryGrenades (Slot: 4, Sub: 1)
Price200$
Damage0
Ammo2 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
 53 - Smoke GrenadeCategoryGrenades (Slot: 4, Sub: 2)
Price300$
Damage0
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
 54 - FlareCategoryGrenades (Slot: 4, Sub: 3)
Price150$
Damage0
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
 55 - BombCategoryExplosives (Slot: 5, Sub: 0)
Price0$ (not buyable)
Damage0
Ammo0 | 0
Rate of Fire0 (0 frame(s) delay)
Range Very Short (0)
WeightLight (SpdM: 0.3)
AccuracyPerfect (V: 0)
 56 - Defuse KitCategoryOther (No Slot)
Price200$
WeightNormal (SpdM: 0)
 57 - KevlarCategoryOther (No Slot)
Price650$
WeightNormal (SpdM: 0)
 58 - Kevlar+HelmCategoryOther (No Slot)
Price1000$
WeightNormal (SpdM: 0)
 59 - Night VisionCategoryOther (No Slot)
Price1250$
WeightNormal (SpdM: 0)
 60 - Gas MaskCategoryOther (No Slot)
Price250$
WeightNormal (SpdM: 0)
 61 - Primary AmmoCategoryOther (No Slot)
Price50$
WeightNormal (SpdM: 0)
 62 - Secondary AmmoCategoryOther (No Slot)
Price50$
WeightNormal (SpdM: 0)
 63 - Planted BombCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 64 - MedikitCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 65 - BandageCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 66 - CoinsCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 67 - MoneyCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 68 - GoldCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 69 - MacheteCategoryMelee (Slot: 3, Sub: 0)
Price0$ (not buyable)
Damage175
Ammo0 | 0
Rate of Fire107 RPM (FD: 14)
Range Very Short (12)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s525 (3 x 175)
DPS Ø625 (3.571 x 175)
 70 - Red FlagCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 71 - Blue FlagCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 72 - Gas GrenadeCategoryGrenades (Slot: 4, Sub: 4)
Price300$
Damage9
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s45 (5 x 9)
DPS Ø45 (5 x 9)
 73 - Molotov CocktailCategoryGrenades (Slot: 4, Sub: 5)
Price300$
Damage4
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s20 (5 x 4)
DPS Ø20 (5 x 4)
 74 - WrenchCategoryMelee (Slot: 3, Sub: 0)
Price0$ (not buyable)
Damage30
Ammo0 | 0
Rate of Fire150 RPM (FD: 10)
Range Very Short (7)
WeightLight (SpdM: 0.75)
AccuracyPerfect (V: 0)
DPS 1s150 (5 x 30)
DPS Ø150 (5 x 30)
 75 - SnowballCategoryGrenades (Slot: 4, Sub: 6)
Price50$
Damage35
Ammo10 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s175 (5 x 35)
DPS Ø175 (5 x 35)
 76 - Air StrikeCategoryGrenades (Slot: 4, Sub: 7)
Price500$
Damage1500
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s7500 (5 x 1500)
DPS Ø7500 (5 x 1500)
 77 - MineCategoryExplosives (Slot: 5, Sub: 8)
Price400$
Damage150
Ammo3 | 0
Rate of Fire150 RPM (FD: 10)
RangeMedium (120)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s750 (5 x 150)
DPS Ø750 (5 x 150)
 78 - ClawCategoryMelee (Slot: 3, Sub: 0)
Price0$ (not buyable)
Damage50 (Secondary: 90)
Ammo0 | 0
Rate of Fire125 RPM (FD: 12)
Range Very Short (9)
WeightLight (SpdM: 0.55)
AccuracyPerfect (V: 0)
DPS 1s200 (4 x 50)
DPS Ø208.333 (4.167 x 50)
 79 - Light ArmorCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 80 - ArmorCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 81 - Heavy ArmorCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 82 - Medic ArmorCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 83 - Super ArmorCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 84 - Stealth SuitCategoryOther (No Slot)
Price0$ (not buyable)
WeightNormal (SpdM: 0)
 85 - ChainsawCategoryMelee (Slot: 3, Sub: 0)
Price0$ (not buyable)
Damage60
Ammo0 | 0
Rate of Fire500 RPM (FD: 3)
Range Very Short (12)
WeightHeavy (SpdM: -0.2)
AccuracyPerfect (V: 0)
DPS 1s960 (16 x 60)
DPS Ø1000 (16.667 x 60)
 86 - Gut BombCategoryGrenades (Slot: 4, Sub: 0)
Price150$
Damage50
Ammo1 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s250 (5 x 50)
DPS Ø250 (5 x 50)
 87 - Laser MineCategoryExplosives (Slot: 5, Sub: 9)
Price400$
Damage150
Ammo3 | 0
Rate of Fire150 RPM (FD: 10)
RangeMedium (200)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s750 (5 x 150)
DPS Ø750 (5 x 150)
 88 - Portal GunCategoryPrimary (Slot: 1, Sub: 0)
Price10000$
Damage0
Ammo0 | -1
Rate of Fire300 RPM (FD: 5)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyPerfect (V: 0)
 89 - Satchel ChargeCategoryExplosives (Slot: 5, Sub: 0)
Price300$
Damage100
Ammo5 | 0
Rate of Fire150 RPM (FD: 10)
RangeFar (320)
WeightLight (SpdM: 0.5)
AccuracyPerfect (V: 0)
DPS 1s500 (5 x 100)
DPS Ø500 (5 x 100)
 90 - M134CategoryPrimary (Slot: 1, Sub: 0)
Price8100$
Damage13
Ammo400 | 800
Rate of Fire750 RPM (FD: 2)
RangeMedium (300)
WeightHeavy (SpdM: -1)
AccuracyBad (V: 5)
DPS 1s325 (25 x 13)
DPS Ø325 (25 x 13)
 91 - FN F2000CategoryPrimary (Slot: 1, Sub: 0)
Price3500$
Damage26
Ammo30 | 90
Rate of Fire500 RPM (FD: 3)
RangeMedium (300)
WeightNormal (SpdM: 0)
AccuracyAverage (V: 2)
DPS 1s416 (16 x 26)
DPS Ø433.333 (16.667 x 26)
```

---

## modes (https://www.cs2d.com/modes.php)

```text
Game Modes       
    
        CS2D has more to offer than just the regular Counter-Strike gameplay! This page gives you an overview of available game modes.
    

    
        
            Game Mode | 
            Teams | 
            Respawn | 
            Round Time Limit | 
        

        
            Standard | 
            Ts vs CTs | 
            No Respawn | 
            Yes | 
        

        
            Deathmatch | 
            No Teams | 
            Respawn | 
            No | 
        

        
            Team Deathmatch | 
            Ts vs CTs | 
            Respawn | 
            No | 
        

        
            Construction | 
            Ts vs CTs | 
            Respawn | 
            No | 
        

        
            Zombies! | 
            Zombies vs Survivors | 
            Respawn as Zombie | 
            Yes | 
        

    
    -->

            
        
            
                Standard
                
                    Offers gameplay as you know it from the original Counter-Strike.
                    Terrorists vs Counter-Terrorists. They fight in rounds with a time limit (about 3-5 minutes per round).
                    When you die you have to wait for the next round to begin.
                    A round ends when the time is over, all players of a team died or when all mission goals have been completed.
                    The standard mission types (DE, CS, AS) only work in this game mode!
                

            
            
            
                
            
        

        
            Best map/mission types:
            
                DE - bomb defuse (Ts try to plant a bomb, CTs try to prevent or defuse)

                CS - hostage rescue (Ts try to keep hostages, CTs try to rescue them)

                AS - VIP assassination (Ts try to kill the VIP, CTs try to escort the VIP)

            
        
    

    
        
            
                
            

            
                Deathmatch
                
                    Simple fast paced action.
                    You are on your own. No teams, no time limit, instant respawn after death.
                    You can also activate an option to let players spawn at random places on the map and not only on designated spawn points.
                    This option makes it possible to use nearly every map with the deathmatch mode.
                

            
        

        
            Best map/mission types:
            
                DM - deathmatch

            
        
    

    
        
            
                Team Deathmatch
                
                    Works like the Standard game mode but without round time limit and players can respawn instantly.
                    There are great map types for this like capture the flag or domination.
                

            

            
                
            
        

        
            Best map/mission types:
            
                DM - deathmatch

                CTF - capture the flag (steal the enemy's flag and bring it to your flag)

                DOM - domination (try to capture and hold all flag points on the map)

            
        
    

    
        
            
                
            

            
                Construction
                
                    Two teams, no rounds, respawn and wrenches!
                    Wrenches can be used to construct buildings like walls, teleporters, barbed wire, dispensers and even automatic turrets.
                    This game mode works best on capture the flag (CTF) or domination (DOM) maps where turrets can be used to protect the flags or domination points.
                    And there are also designated maps for construction (CON).
                    It actually is fun on nearly every map though.
                

            
        

        
            Best map/mission types:
            
                CON - construction

                DM - deathmatch

                CTF - capture the flag

                DOM - domination

            
        
    

    
        
            
                Zombies!
                
                    Brrraaaaaaaains! Everyone loves zombies - that's why CS2D has a game mode with them!
                    There are two teams: Survivors (CTs) and zombies (Ts).
                    Survivors have to do what their name implies: Survive. Simple, huh?
                    No, because all zombies have to try to kill the survivors within a certain time limit. Every survivor who gets killed by a zombie respawns as a zombie.
                    The survivor team wins when at least one person survives until the time is over. Zombies win when everyone turned into a zombie.
                    This mode should be played on special ZM maps because survivors need weapon/ammo/health items which are lying on the map to stand a chance!
                

            
            
            
                
            
        

        
            Best map/mission types:
            
                ZM - zombies

            
        
    

    
    Missions & Map Types

    
        There are different missions and map types which can be chosen independently of the game mode.
        Some game modes need special map types to work well though. The mission/type of a map can be recognized by taking a look at the prefix of the map name (the part before the underscore).
        When you create your own new map with the map editor you should always choose the correct prefix for the map name so others know the type of your map without even loading it.
    

    Standard Missions
    
        The following are standard missions and map types of CS2D. Many require special entities to make them work properly.
    

    
        AS - Assassination
        
            Counter-Terrorists spawn with one V.I.P. player each round who has to be escorted to a safe escape zone.
            Terrorists have to assassinate the V.I.P. or to prevent him from reaching the escape zone in order to win.
            The V.I.P. is unable to buy or collect weapons/equipment and starts with a pistol and a 200 armor point kevlar west.
        

        
        
            Entities:
            
                Info_VIP Spawn point for the V.I.P.

                Info_EscapePoint Escape point for the V.I.P.

            
        
    

    
        CS - Hostage Rescue
        
            The terrorists took hostages and now they have to prevent counter-terrorists from rescuing the hostages.
            Counter-Terrorists can rescue hostages by telling them to follow them and leading them to either the counter-terrorist spawn or designated hostage rescue zones.
            Hostages are non player characters (NPCs). Only counter-terrorists can use them to make them move. There are commonly 4 hostages on a CS map.
        

        
        
            Entities:
            
                Info_Hostage Spawn point for hostages (commonly four in total per map, in two groups of two)

                Info_RescuePoint Point where CTs have to bring hostages (optional, CT spawns work like rescue points as well)

            
        
    

    
        DE - Bomb Defuse
        
            Time to blow something up - at least that's what the terrorists are thinking.
            One terrorist starts with a bomb which has to be planted at a bomb spot (there are commonly 2 differnt spots to plant the bomb).
            Counter-Terrorists have to prevent that.
            They can either protect the spots and make sure that no bomb is planted at all or they can try to defuse planted bombs before they blow up.
        

        
        
            Entities:
            
                Info_BombSpot Spot where Ts can plant the bomb (there are commonly exactly two different bomb spots per map)

            
        
    

    
        CTF - Capture the Flag
        
            Each team has a flagpole with a flag close to the own spawn. The goal is to bring both, the own flag and the flag of the opposing team, to the own flagpole.
            You can only carry and move the flag of the other team. When you drop that flag, players of the other team can instantly return it to their base by simply touching it.
        

        
        
            Entities:
            
                Info_CTF_Flag Flag to capture (exactly two per map - one T flag and one cT flag)

            
        
    

    
        DOM - Domination
        
            There are multiple domination points (flag poles) on the map. A team has to dominate all of them in order to win.
            A point can be dominated by simply touching it while no player of the other team is close to it.
            There are also points which require 1 or 2 players of the own team to be close in order to dominate it.
        

        
        
            Entities:
            
                Info_Dom_Point Domination point (there is no standard for the number but three is recommended as minimum)

            
        
    

    
        ZM - Zombies
        
            Commonly used with "Zombies!" game mode only.
            T spawns will be for the zombies, CT spawns for the survivors.
            Good zombie maps provide some items for survivors and prevent that survivors can spawn kill zombies.
        

        
        
            Entities:
            
                Env_Item Items like weapons, ammo and health packs for survivors (optional but highly recommended)

                Info_TeamGate Invisible barriers to prevent that survivors can spawn kill zombies (optional)

            
        
    

    
        DM - Deathmatch
        
            Maps designed to be played in "Deathmatch" game mode. Commonly with some items lying around.
        

        
        
            Entities:
            
                Env_Item Items like weapons, ammo and health packs (optional)

            
        
    

    
        CON - Construction
        
            Maps designed to be played in "Construction" game mode. Commonly with some items (especially money) lying around.
        

        
        
            Entities:
            
                Env_Item Items like money which can be spent for buildings (optional)

            
        
    

    Fun Missions
    
        
            There are also some additional fun mission/map types (this is an incomplete list which only contains some very popular map types):
        

        
            FY - Fight Yard Arena to simply fight and have fun

            HE - High Explosives Many HE grenades and/or other explosives

            KA - Knife Arena Knifes or other melee weapons only

            AWP - AWP Arena AWPs only

            AIM - Aiming Training Practice aiming

            and many more ...
```

---

## cheatsheets (https://www.cs2d.com/cheatsheets.php)

```text
Cheat Sheets    
    
        Nope, this page is not about cheating in CS2D. It just contains a few images/texts (cheat sheets) which might be quite useful for everyone who wants to modify/extend the game.
        Open the images in a new tab if you want to save/print them.    
        
            Weapons & Item IDs can be used for commands like equip

            Dynamic Objects IDs can be used for commands like spawnobject

            NPC Player Values for NPC dynamic objects (see spawnobject)

            Game Mode IDs - e.g. for sv_gamemode

            Team IDs - note that the VIP has its own ID even though it's basically a CT

            Kill IDs can be used to check how a player died (Lua hooks: die & kill)

            CS2D Map Format Specification (TXT)

        
    

    
    More useful things you should know about
    
        CS2D.com offers detailed references for important stuff. Maybe you did not know about the following pages yet?!
        
            Console Command Reference

            Lua Hook Reference

            Lua Command Reference

            All Weapons & Items with attributes

            Entities with attributes
```

---

