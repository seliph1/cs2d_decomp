-- Dynamic light reference (flashlight [57], muzzle lights): night (sv_daylighttime 90), player 1 spawned at
-- (1904, 432) (the CT spawn of de_dust) with an AK-47; the driver then switches the flashlight on (F) and fires.
-- Prints "FL <id> <state>" for every flashlight switch the server accepts.
parse("sv_daylighttime 90")
parse("mp_flashlight 1")
addhook("team","fl_team")
function fl_team(id) if id == 1 then timer(1000,"fl_spawn") end end
function fl_spawn()
  parse("spawnplayer 1 1904 432")
  parse("equip 1 30")
  parse("setweapon 1 30")
  print("FL spawn")
end
addhook("flashlight","fl_switch")
function fl_switch(id, state) print("FL "..id.." "..state) end
