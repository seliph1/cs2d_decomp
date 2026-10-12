-- Hover text probe: on the first human spawn adds a CT and a T bot (bot_freeze 1); every second puts the CT bot
-- 3 tiles and the T bot 6 tiles east of the player, at 60 / 35 health, and keeps a Turret 9 tiles east (team 2)
addhook("spawn","hv_spawn")
hv_id=0
function hv_spawn(id)
  if hv_id==0 and player(id,"bot")==false then
    hv_id=id
    parse("bot_freeze 1")
    parse("bot_add_ct")
    parse("bot_add_t")
    timer(1000,"hv_place","",0)
  end
end
function hv_place()
  if player(hv_id,"health")<=0 then return end
  local px,py=player(hv_id,"x"),player(hv_id,"y")
  for _,b in ipairs(player(0,"table")) do
    if player(b,"bot") and player(b,"health")>0 then
      local ct=player(b,"team")==2
      parse("setpos "..b.." "..(px+(ct and 96 or 192)).." "..py)
      parse("sethealth "..b.." "..(ct and 60 or 35))
    end
  end
  local tx,ty=player(hv_id,"tilex"),player(hv_id,"tiley")
  if #object(0,"table")<83 then parse("spawnobject 8 "..(tx+9).." "..ty.." 0 0 2 0") end
end
