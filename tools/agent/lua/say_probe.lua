-- Chat hook probe: registers all four say hooks and prints every call.
function sp_join(id) print("SP join "..id) end
addhook("join", "sp_join")

function sp_say(id, msg) print("SP say "..id.." ["..tostring(msg).."]") return 0 end
addhook("say", "sp_say")

function sp_sayteam(id, msg) print("SP sayteam "..id.." ["..tostring(msg).."]") return 0 end
addhook("sayteam", "sp_sayteam")

function sp_sayutf8(id, msg) print("SP sayutf8 "..id.." ["..tostring(msg).."]") return 0 end
addhook("sayutf8", "sp_sayutf8")

function sp_sayteamutf8(id, msg) print("SP sayteamutf8 "..id.." ["..tostring(msg).."]") return 0 end
addhook("sayteamutf8", "sp_sayteamutf8")

-- Bot chat drives cs2d_sv_chat without a client (ai_say / ai_sayteam).
function sp_boot()
    parse("bot_add_t")
    timer(2500, "sp_chat")
end
function sp_chat()
    ai_say(1, "hello world")
    ai_sayteam(1, "team hello")
    ai_say(1, "ol\195\161 mundo")
end
timer(2000, "sp_boot")
