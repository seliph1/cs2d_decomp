# CS2D Entities Local Database (Scraped from cs2d.com/entities.php)

Total Entities: 50

## [0] Info_T (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_T

```text
ID 0 - Info_T (T)InfoTerrorist (or zombie) spawn point.

Attention: At least one Info_T must be on each map. Otherwise the map is unplayable.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=spawn rotation
```

---

## [1] Info_CT (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_CT

```text
ID 1 - Info_CT (CT)InfoCounter-Terrorist (or survivor) spawn point.

Attention: At least one Info_CT must be on each map. Otherwise the map is unplayable.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=spawn rotation
```

---

## [2] Info_VIP (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_VIP

```text
ID 2 - Info_VIP (VIP)InfoVIP spawn point. Required for AS_ maps (assassination) in standard game mode. One counter-terrorist will be spawned as VIP. The VIP can't buy anything, can only use his pistol and has 200 armor. Counter-Terrorists must protect the VIP and lead him to the escape point (Info_EscapePoint). Terrorists must prevent this by either blocking the way or killing the VIP.

Note: This entity should only be used on as_ maps.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=spawn rotation
```

---

## [3] Info_Hostage (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_Hostage

```text
ID 3 - Info_Hostage (H)InfoHostage spawn point. Hostages are required for CS_ maps in standard game mode. Counter-Terrorists can use hostages so they follow them. They have to lead them to rescue points (Info_RescuePoint). Terrorists can't interact with hostages. Hurting/killing hostages leads to a monetary penalty.

Note: This entity should only be used on cs_ maps.

Note: There are typically 4 hostages on cs_ maps.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=spawn rotation
ints[1]=hostage look
```

---

## [4] Info_RescuePoint (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_RescuePoint

```text
ID 4 - Info_RescuePoint (RPoint)InfoPoint where counter-terrorists have to bring hostages (Info_Hostage) in order to rescue them. On cs_ maps in standard game mode.

Note: This entity should only be used on cs_ maps.

Note: This entity only makes sense in conjunction with at least one Info_Hostage entity
```

---

## [5] Info_BombSpot (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_BombSpot

```text
ID 5 - Info_BombSpot (BSpot)InfoSpot where terrorists have to plant the bomb on de_ maps in standard game mode. There are commonly two bomb spots on a regular de_ map.

Note: This entity should only be used on de_ maps.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=explosion range
```

---

## [6] Info_EscapePoint (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_EscapePoint

```text
ID 6 - Info_EscapePoint (EPoint)InfoPoint where VIPs have to go to in order to escape (and for the counter-terrorists to win). For as_ maps in standard game mode.

Note: This entity should only be used on as_ maps.

Note: Only makes sense if there is at least one Info_EscapePoint entity.
```

---

## [8] Info_Animation (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_Animation

```text
ID 8 - Info_Animation (Anim)InfoAnimates a tile. Each tile on the map which equals the start tile in the settings of this entity will get animated. It will change its look in the specified speed until it reaches the end tile number. Then the animation will start again from the start tile number.

Attention: Never use the same start tile number in multiple different Info_Animation entities! This is not supported.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=start tile/frame index/number
ints[1]=end tile/frame index/number
ints[2]=animation speed (milliseconds per frame)
```

---

## [9] Info_Storm (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_Storm

```text
ID 9 - Info_Storm (Storm)InfoCreates a storm effect which influences most particles on the map.

Note: This entity should only be used once per map.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=storm direction (angle)
ints[1]=storm speed
```

---

## [10] Info_TileFX (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_TileFX

```text
ID 10 - Info_TileFX (TileFX)InfoCreates a tile effect (scrolling, distortion, waves).

Attention: You should only create one Info_TileFX per tile number!
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=tile/frame number/index
ints[1]=effect
ints[2]=scroll x speed
ints[3]=scroll y speed
```

---

## [11] Info_NoBuying (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_NoBuying

```text
ID 11 - Info_NoBuying (NoBuying)InfoDisables buying on the map.

Note: This entity should only be used once per map.
```

---

## [12] Info_NoWeapons (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_NoWeapons

```text
ID 12 - Info_NoWeapons (NoWpns)InfoDisables weapons.

Note: This entity should only be used once per map.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=0 to remove old weapons (strip weapons from players on roundstart), 1 to keep them
```

---

## [13] Info_NoFOW (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_NoFOW

```text
ID 13 - Info_NoFOW (NoFOW)InfoDisables fog of war on the map even if it is enabled in the settings.

Note: This entity should only be used once per map.
```

---

## [14] Info_Quake (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_Quake

```text
ID 14 - Info_Quake (Quake)InfoLets the map shake constantly. Trigger it to enable/disable the effect.

It can make sense to have multiple of these entities on the map when they are used with appropriate triggers.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=quake power
```

---

## [15] Info_CTF_Flag (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_CTF_Flag

```text
ID 15 - Info_CTF_Flag (CTF)InfoA flag for ctf_ (capture the flag) maps. 

Attention: There should only be one terrorist and one counter-terrorist flag on a ctf_ map. Everything else may lead to problems.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=flag type (0=red terrorist, 1=blue counter-terrorist)
```

---

## [16] Info_OldRender (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_OldRender

```text
ID 16 - Info_OldRender (OldRender)InfoSwitches the map rendering engine back to a compatibility mode for old CS2D versions. May be required to make some old maps work properly. Usage is NOT recommended for new maps.

Note: This entity should only be used once per map.

Attention: This entity has been deprecated in CS2D v1.0.0.4. It is still supported but it does not appear in the editor entity list anymore in newer versions of CS2D.
```

---

## [17] Info_Dom_Point (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_Dom_Point

```text
ID 17 - Info_Dom_Point (DOM)InfoA domination point for dom_ maps. It is rendered as a flag pole with either no flag or a blue or a red flag depending on the team which dominates the point. Little white stripes on the flag pole indicate how many players are required to dominate the flag. The flag is dominated as soon as that many players of a team are close to it without any enemies being close.

There are commonly several domination points on a good dom_ map.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=initial state (0=not dominated, 1=dominated by Ts, 2=dominated by CTs)
ints[1]=min. players for domination (0=1, 1=2, 2=3)
```

---

## [18] Info_NoBuildings (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_NoBuildings

```text
ID 18 - Info_NoBuildings (NoBuild)InfoDefines an area where building is not allowed. The area starts at the position of the entity. An extra setting allows to control if mines and portals will be blocked as well.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=area width (tiles)
ints[1]=area height (tiles)
ints[2]=allow mines/portals? (1=yes, 0=block them as well)
```

---

## [19] Info_BotNode (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_BotNode

```text
ID 19 - Info_BotNode (BNode)InfoDefines a "point of attraction" for bots. Bots will visit this point from time to time. Use it to mark important spots on the map. Place 2 or more close together to increase the attraction of a spot.

Note: This entity is NOT required to make maps work with bots! It's meant for fine tuning only (e.g. if bots never take certain alternative routes because they are a detour)

Attention: Do NOT use these entities like waypoints! Use as few as possible. Only place them on important spots. Using too many will most likely result in worse bot behavior.
```

---

## [20] Info_TeamGate (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_TeamGate

```text
ID 20 - Info_TeamGate (TGate)InfoDefines an area which kills members of a certain team when they try to pass. The area starts at the position of the entity. Some exceptions can also be defined with the settings.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=area width (tiles)
ints[1]=area height (tiles)
ints[2]=team to let pass (0=nobody, 1=terrorists, 2=counter-terrorists)
ints[3]=don't kill flag carrier (0/1)
ints[4]=don't kill bomb carrier (0/1)
ints[5]=don't kill defuse kit carrier (0/1)
ints[6]=don't kill VIP (0/1)
ints[7]=don't kill gas mask wearer (0/1)
ints[8]=don't kill night vision wearer (0/1)
```

---

## [21] Env_Item (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Item

```text
ID 21 - Env_Item (Item)InfoSpawns a certain item on the map at the position of this entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=item type
ints[1]=spawn mode (0=one time, 1=instant/infinity, 2=every x seconds, 3=on trigger only)
ints[2]=spawn delay (seconds, for spawn mode 2)
```

---

## [22] Env_Sprite (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Sprite

```text
ID 22 - Env_Sprite (Spr)InfoRenders a sprite on the map.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=x size
ints[1]=y size
ints[2]=x offset
ints[3]=y offset
ints[4]=rotation
ints[5]=color red (0-255)
ints[6]=color green (0-255)
ints[7]=color blue (0-255)
ints[8]=FX mode (deprecated, does nothing)
ints[9]=Blend mode (1=solid, 2=masked, 3=light, 4=shade, 5=auto, 6=grayscale)
strs[0]=image file path
strs[1]=transparency (0.0-1.0)
strs[2]=mask mode
strs[3]=rotation speed
strs[4]=sprite covered by player? ("" for no, "1" for yes)
strs[5]=hide if nightvision is on? ("" for no, "1" for yes)
strs[6]=3D height in pixels ("" or "0" for no 3D height)
strs[7]=affect lighting? ("" for no, "1" for yes)
strs[8]=is decoration? ("" for no, "1" for yes)
```

---

## [23] Env_Sound (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Sound

```text
ID 23 - Env_Sound (Snd)InfoPlays a sound on the map. Either at the position of the entity or everywhere.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=play only when triggered (0/1)
ints[1]=do NOT loop (0/1)
ints[2]=play everywhere (0/1)
strs[0]=sound file path
strs[1]=sound volume (0.0-1.0)
```

---

## [24] Env_Decal (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Decal

```text
ID 24 - Env_Decal (Dcl)InfoRenders a decal on the map at the position of the entity.

Attention: Env_Decal entities are NOT accessible via any command or Lua (ignore the  text in the section below. It is wrong). This is because they use a different system internally. Use Env_Image or Env_Sprite if you need images which can be modified/accessed using Lua.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=decal frame number
ints[1]=color red (0-255)
ints[2]=color green (0-255)
ints[3]=color blue (0-255)
ints[4]=rotation
strs[0]=transparency (0.0-1.0)
```

---

## [25] Env_Breakable (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Breakable

```text
ID 25 - Env_Breakable (Brkbl)InfoCreates a breakable tile on the map at the position of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=tile/frame number/index
ints[1]=health
ints[2]=material (0=wood, 1=stone, 2=metal, 3=flesh, 4=glass, 5=nothing)
ints[3]=explode (0/1)
ints[4]=explosion range
ints[5]=explosion power
ints[6]=shadow type (0=wall, 1=none, 2=obstacle)
strs[0]=transparency (0.0-1.0)
```

---

## [26] Env_Explode (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Explode

```text
ID 26 - Env_Explode (Expl)InfoCauses an explosion on the map at the position of the entity. The explosion is triggered on movement or when the entity is triggered.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=explosion range
ints[1]=explosion power
ints[2]=do NOT explode on movement (0/1)
```

---

## [27] Env_Hurt (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Hurt

```text
ID 27 - Env_Hurt (Hurt)InfoDefines an area in which players are damaged. The area starts at the tile positions.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=damage per second (or on movement)
ints[1]=damage on (0=time, 1=movement, 2=both)
ints[2]=area width (tiles)
ints[3]=area height (tiles)
```

---

## [28] Env_Image (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Image

```text
ID 28 - Env_Image (Img)InfoDraws an image. Images have less options than sprites and are normally drawn on the ground. The origin of the image is the entity position but an offset can be defined in the settings of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=x offset (pixels)
ints[1]=y offset (pixels)
ints[2]=cover players? (0/1)
ints[3]=3D height (pixels)
ints[4]=is decoration? (0/1)
strs[0]=image file path
```

---

## [29] Env_Object (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Object

```text
ID 29 - Env_Object (Obj)InfoCreates an object at the tile position. These objects are commonly for decoration only.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=object type (see editor for full list)
ints[1]=color red (0-255)
ints[2]=color green (0-255)
ints[3]=color blue (0-255)
ints[4]=rotation
strs[0]=size (0.1-2.0, 1.0 for original size)
strs[1]=transparency (0.0-1.0)
```

---

## [30] Env_Building (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Building

```text
ID 30 - Env_Building (Build)InfoCreates a building (or other "Stuff" - see list below) at the tile position.

List of buildings with type IDs (Class 1 and 2 only):

Note: In case you want to place a laser mine you have to place the entity on a floor tile next to a wall tile. In the properties you have to set the direction of the entity to the direction you want the beam to go (only 0, 90, 180 or 270 degree depending on where the wall is). The laser mine will blow up instantly if you don't adjust these things properly.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=building type ID
ints[1]=team (0=neutral, 1=terrorists, 2=counter-terrorists)
ints[2]=rotation (affects barbed wire and laser mine only)
```

---

## [31] Env_NPC (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_NPC

```text
ID 31 - Env_NPC (NPC)InfoSpawns an NPC at the position of the entity. Normally NPCs are spawned on start (after loading the map / on roundstart) automatically. They can also be spawned by triggering the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=rotation
ints[1]=type (see editor list)
ints[2]=health
ints[3]=spawn on start (0=yes, 1=no)
```

---

## [32] Env_Room (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Room

```text
ID 32 - Env_Room (Room)InfoDefines a room for the light engine. The room starts at the position of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=area width (tiles)
ints[1]=area height (tiles)
ints[2]=shadow height (pixels)
ints[3]=color red (0-255)
ints[4]=color green (0-255)
ints[5]=color blue (0-255)
ints[6]=darken walls at edges? (0/1)
```

---

## [33] Env_Light (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_Light

```text
ID 33 - Env_Light (Light)InfoCreates a light for the light engine. Must be in a room (Env_Room) or close to a room to work.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=light range (0-256)
ints[1]=color red (0-255)
ints[2]=color green (0-255)
ints[3]=color blue (0-255)
ints[4]=start angle (0-359)
ints[5]=end angle (0-359)
ints[6]=rotation speed
ints[7]=copy of ints[4] with current rotation applied (runtime, not exposed in editor)
ints[8]=copy of ints[5] with current rotation applied (runtime, not exposed in editor)
strs[0]=additive (0.0-1.0)
strs[1]=x offset
strs[2]=y offset
strs[3]=rotation offset
```

---

## [34] Env_LightStripe (env)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Env_LightStripe

```text
ID 34 - Env_LightStripe (LStripe)InfoCreates a light stripe which can be used for the area between a room and no room for a soft transition. The stripe area begins at the entity positions.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=area width (tiles)
ints[1]=area height (tiles)
ints[2]=direction (clockwise: 0=top, 1=right, 2=bottom, 3=left)
ints[3]=color red (0-255)
ints[4]=color green (0-255)
ints[5]=color blue (0-255)
```

---

## [50] Gen_Particles (gen)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Gen_Particles

```text
ID 50 - Gen_Particles (Particles)InfoSpawns pixel particles from the center of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=lifetime min
ints[1]=lifetime max
ints[2]=x speed min
ints[3]=x speed max
ints[4]=y speed min
ints[5]=y speed max
strs[0]=RGB color encoded as int
```

---

## [51] Gen_Sprites (gen)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Gen_Sprites

```text
ID 51 - Gen_Sprites (Sprites)InfoSpawns sprite particles from the center of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=sprite frame
ints[1]=color red (0-255)
ints[2]=color green (0-255)
ints[3]=color blue (0-255)
ints[4]=x speed min
ints[5]=x speed max
ints[6]=y speed min
ints[7]=y speed max
ints[8]=sprite type
strs[0]=transparency (0.0-1.0)
```

---

## [52] Gen_Weather (gen)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Gen_Weather

```text
ID 52 - Gen_Weather (Weather)InfoCreates weather effects on the map. Rain, snow and thunder will be active for the entire map and should only be used once on the map. All the other effects are local and can be used multiple times.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=weather type (0=rain, 1=snow, 2=fog, 3=dust, 4=smog, 5=thunder)
ints[1]=particles (1-30)
```

---

## [53] Gen_FX (gen)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Gen_FX

```text
ID 53 - Gen_FX (FX)InfoCreates an effect in an area around the position of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=effect type (0=waves, 1=smoke, 2=fire, 3=steam)
ints[1]=x range min
ints[2]=x range max
ints[3]=y range min
ints[4]=y range max
ints[5]=spawn rate (ms)
```

---

## [70] Func_Teleport (func)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Func_Teleport

```text
ID 70 - Func_Teleport (Tel)InfoTeleports players when they step on the position of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=target tile x (teleport destination)
ints[1]=target tile y (teleport destination)
```

---

## [71] Func_DynWall (func)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Func_DynWall

```text
ID 71 - Func_DynWall (DWall)InfoCreates a dynamic wall at the position of the entity. This wall can be disabled/enabled by triggering the entity. Useful for things like doors.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=tile/frame number/index
ints[1]=behavior (0=wall, 1=obstacle, 2=wall w/o shadow, 3=obstacle w/o shadow, 4=floor+tile behavior)
ints[2]=close only if blocked? (0/1)
strs[0]=transparency (0.0-1.0)
```

---

## [72] Func_Message (func)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Func_Message

```text
ID 72 - Func_Message (MSG)InfoDraws a message on the screen when triggered.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=display time in seconds
ints[1]=position (0=center, 1=left/chat)
ints[2]=color red (0-255)
ints[3]=color green (0-255)
ints[4]=color blue (0-255)
strs[0]=message text
```

---

## [73] Func_GameAction (func)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Func_GameAction

```text
ID 73 - Func_GameAction (GAction)InfoPerforms the specified action when triggered.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=action (see editor list)
strs[0]=optional X parameter (depending on action)
```

---

## [80] Info_NoWeather (info)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Info_NoWeather

```text
ID 80 - Info_NoWeather (NoWeather)InfoDefines an area where weather effects are disabled. The area starts at the position of the entity.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=area width (tiles)
ints[1]=area height (tiles)
```

---

## [90] Trigger_Start (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Start

```text
ID 90 - Trigger_Start (Start)InfoTriggers entities directly at round start.
```

---

## [91] Trigger_Move (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Move

```text
ID 91 - Trigger_Move (Move)InfoTriggers entities when a player walks over it.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=triggered by (0=everyone, 1=terrorists only, 2=counter-terrorists only)
```

---

## [92] Trigger_Hit (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Hit

```text
ID 92 - Trigger_Hit (Hit)InfoTriggers entities when it is hit. Must be placed at a wall to work properly.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=triggered by (0=everyone, 1=terrorists only, 2=counter-terrorists only)
```

---

## [93] Trigger_Use (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Use

```text
ID 93 - Trigger_Use (Use)InfoTriggers entities when it is used with the use key (E by default).
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=button style (see editor list)
ints[1]=button alignment/direction (0=top, 1=bottom, 2=left, 3=right)
ints[2]=triggered by (0=everyone, 1=terrorists only, 2=counter-terrorists only)
ints[3]=re-use delay (ms)
```

---

## [94] Trigger_Delay (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Delay

```text
ID 94 - Trigger_Delay (Delay)InfoForwards a trigger signal after a specified time.

In other words: As soon as this entity is triggered, it will wait for the specified delay. After this delay it will trigger all entities in its trigger field.

Note: You can also enter floating point values in the field! (supported since version 1.0.0.3)

Examples for valid delay values:

1 = 1 second

0.5 = half a second

0.1 = 1/10 of a second (100 ms)

0.01 = 1/100 of a second (10 ms)

0.001 = 1/1000 of a second (1 ms)

Note: If the value is in millisecond precision, it will be saved in ints[1]. If it is in second precision, it will be saved in ints[0].

Note: When triggered the entity will check once each frame if the delay is over. Therefore the actual delay can differ by a few milliseconds. See Lua timer command for details on this behavior.

Attention: If ints[1] is > 0, the game will use this value for the delay. The value of ints[0] will be ignored in that case.
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=delay in seconds (used when entering an integer in the editor)
ints[1]=delay in milliseconds (used when entering a floating point number in the editor)
```

---

## [95] Trigger_Once (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_Once

```text
ID 95 - Trigger_Once (Once)InfoForwards a trigger signal only once during a round. All following trigger signals will not be forwarded.
```

---

## [96] Trigger_If (trigger)
- **URL**: https://www.cs2d.com/entities.php?cat=all&entity=Trigger_If

```text
ID 96 - Trigger_If (If)InfoEvaluates a condition and runs its triggers when the condition is true. You can either choose from a set of predefined conditions (see list below) or run a custom Lua condition. The Lua evaluation runs in the same Lua space as all other Lua scripts (map scripts and server scripts) so you can use functions and variables from these scripts.

Condition types (Trigger entities in trigger field if...):

0 - Lua expression (strs[0]) is true (mp_luamap must be enabled to make this work)

1 - at least A player(s) are in area B,C,D,E (x, y, width, height in tiles)

2 - max A player(s) area in area B,C,D,E (x, y, width, height in tiles)

3 - entity state at A, B (position in tiles) is 0 (visible/enabled)

4 - entity state at A, B (position in tiles) is 1 (hidden/disabled)

5 - at least A terrorists are alive

6 - at least A counter-terrorists are alive

7 - max A terrorists are alive

8 - max A counter-terrorists are alive

9 - remaining round time in seconds is greater/equal A (always triggers in game modes without time limit)

10 - remaining round time in seconds is less/equal A (never triggers in game modes without time limit)

11 - at least A item(s) are at B,C (position in tiles)

12 - max A item(s) are at B, C (position in tiles)

13 - any building is at A, B (position in tiles)

14 - no building is at A, B (position in tiles)

Here are some examples for the Lua condition evaluation:

Sample 1: Always trigger

true

Sample 2: Never trigger

false

Sample 3: Only trigger if the Lua variable "sample" has the value 5

sample==5

Sample 4: Only trigger if the Lua variable "sample" has a value between 3 and 7 (4,5,6)

sample>3 and sample

Sample 5: Only trigger if there is a player with ID 1 on the server

player(1,"exists")

Attention: The server setting mp_luamap must be 1 to make Lua condition evaluation work (other conditions work always).

Attention: This entity can be abused to execute malicious Lua code. For this reason mp_luamap is set to 0 by default!
Values / PropertiesThe following values can be set in the entity properties menu of the map editor and can be read using the Lua command entity.
ints[0]=Condition type (0 = Lua, > 0: Predefined condition)
ints[1]=Condition parameter A (in most cases: amount or X coordinate in tiles)
ints[2]=Condition parameter B (in most cases: X or Y coordinate in tiles)
ints[3]=Condition parameter C (in most cases: Y coordinate in tiles)
ints[4]=Condition parameter D (in most cases: area width in tiles)
ints[5]=Condition parameter E (in most cases: area height in tiles)
strs[0]=Lua expression to evaluate (in case condition type is 0)
```

---

