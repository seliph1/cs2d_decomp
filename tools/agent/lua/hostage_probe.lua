-- hostage probe (cs_office): puts the joining player on CT on hostage 1's tile (3,5), then logs the use hook
-- and every hostage position each second (lines start with HG)
addhook("join","hg_join")
function hg_join(id)
	timer(500,"hg_team",tostring(id))
end
function hg_team(id)
	parse("makect "..id)
	parse("sv_restartround 1")
	timer(3000,"hg_spawn",id)
end
function hg_spawn(id)
	parse("spawnplayer "..id.." 112 176")
	print("HG spawned "..id)
	timer(1000,"hg_dump","",0)
end
function hg_dump()
	local s = "HG pos"
	for _,i in ipairs(hostage(0,"table")) do
		s = s.." | "..i..": "..hostage(i,"x")..","..hostage(i,"y").." hp "..hostage(i,"health").." f "..hostage(i,"follow")
	end
	print(s)
end
addhook("use","hg_use")
function hg_use(id,event,data,x,y)
	print("HG use "..id.." "..event.." "..data.." "..x.." "..y)
end
addhook("hostageuse","hg_huse")
function hg_huse(id,hid,mode)
	print("HG hostageuse "..id.." "..hid.." "..mode)
end
