-- Night lighting reference: the server sets sv_daylighttime (default 90 = night) and spawns player 1 at
-- (440, 260) on fun_roleplay, then at the other positions of light_probe.lua every 4 s.
LP_DAY = 90
parse("sv_daylighttime "..LP_DAY)
dofile("tools/agent/lua/light_probe.lua")
