-- Does the original still run sayutf8 when say returns 1, and what suppresses the message?
function ss_say(id, msg) print("SS say "..id.." ["..tostring(msg).."]") return 1 end
addhook("say", "ss_say")
function ss_sayutf8(id, msg) print("SS sayutf8 "..id.." ["..tostring(msg).."]") return 0 end
addhook("sayutf8", "ss_sayutf8")
function ss_sayteam(id, msg) print("SS sayteam "..id.." ["..tostring(msg).."]") return 0 end
addhook("sayteam", "ss_sayteam")
function ss_sayteamutf8(id, msg) print("SS sayteamutf8 "..id.." ["..tostring(msg).."]") return 1 end
addhook("sayteamutf8", "ss_sayteamutf8")

function ss_boot()
    parse("bot_add_t")
    timer(2500, "ss_chat")
end
function ss_chat()
    ai_say(1, "hello world")
    ai_sayteam(1, "team hello")
end
timer(2000, "ss_boot")
