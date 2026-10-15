-- Dynamic light reference (laser and explosion lights): night (sv_daylighttime 90), player 1 spawned at (1904, 432)
-- (the CT spawn of de_dust) holding a Laser; 5 s later the HE grenade is selected for the driver to throw.
parse("sv_daylighttime 90")
addhook("team","ex_team")
function ex_team(id) if id == 1 then timer(1000,"ex_spawn") end end
function ex_spawn()
  parse("spawnplayer 1 1904 432")
  parse("equip 1 51")
  parse("equip 1 45")
  parse("setweapon 1 45")
  print("EX spawn")
  timer(5000,"ex_he")
end
function ex_he()
  parse("setweapon 1 51")
  print("EX he")
end
