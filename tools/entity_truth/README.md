# Entity ground truth from cs2d_dedicated

Records how the official dedicated server changes entity states when every trigger name of a map
is fired, so `cs2d_entities.c` can be checked against it (`test_entities_prison` in `tests/test_cs2d.c`).

1. Dump the map entities with our loader:

       gcc -Iinclude tools/entity_truth/dump_entities.c src/cs2d_map.c -o dump_entities.exe
       dump_entities.exe maps/prison_funst3r.map > prison_ents.txt

2. Generate the server Lua sweep and the shared plan:

       python tools/entity_truth/gen_sweep.py prison_ents.txt plan.txt sweep.lua

   The sweep fires `trigger <name>` for each name, samples `entity(x,y,"state")` 0.5 s later and
   then every 1.0 s until the longest Trigger_Delay chain of that name has finished.

3. Run it on the dedicated server, offline (no U.S.G.N. serverlist) and on a spare port.
   `-lua` is relative to `sys/lua/`:

       cs2d_dedicated.exe -offline -map prison_funst3r -port 36971 -lua <path to sweep.lua> > sweep.log

   Wait for the `ENTDONE` line (about 8 minutes for prison_funst3r), then stop the server.

4. Convert the log into the versioned fixture:

       python tools/entity_truth/sweep_to_fixture.py sweep.log plan.txt tests/data/prison_funst3r_triggers.txt
