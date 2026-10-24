-- Captures what the dedicated prints and sends when a player leaves or is kicked (the original client is id 1;
-- our client is id 2 once sv_maxclientsperip allows it). LT_KICK=1 kicks id 2 (ours) at 24 s; set LT_CMD to change the command.
-- Used by tools/agent/capture_two.sh; look for "LT" lines in the dedicated log and [253]/[240] in the proxy logs.
parse("mp_maxclientsip 5")
parse("sv_password")  -- sys/server.cfg has a local password; clear it for the capture (cfg is restored by capture_two.sh)
addhook("join","lt_join")
addhook("leave","lt_leave")
addhook("say","lt_say")
function lt_join(id) print("LT join "..id.." "..player(id,"name")) end
function lt_leave(id,r) print("LT leave "..id.." reason="..tostring(r)) end
function lt_say(id,t) print("LT say "..id.." "..t) end
function lt_kick() print("LT kick now"); parse(os.getenv("LT_CMD") or "kick 2 \"test reason\"") end
if os.getenv and os.getenv("LT_KICK") == "1" then timer(24000,"lt_kick","",1) end
