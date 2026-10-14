-- hudtxt probe: shows HUD texts with every option, then animates them (hudtxtmove / alphafade / colorfade)
-- and clears them. Output lines start with HT.
function ht_show()
	parse('hudtxt 0 "Left top plain" 10 120 0 0 13')
	parse('hudtxt 1 "\169255000000Red centered" 425 160 1 0 20')
	parse('hudtxt 2 "Right bottom size 30" 840 260 2 2 30')
	parse('hudtxt 3 "\169000255000Green \169000128255Blue \169255255255white" 425 300 1 1 16')
	parse('hudtxt 4 "Big 60" 20 330 0 0 60')
	parse('hudtxt2 1 5 "hudtxt2 to player 1" 10 420 0 0 13')
	print("HT show")
end
function ht_anim()
	parse('hudtxtmove 0 0 2000 300 120')
	parse('hudtxtalphafade 0 1 2000 0.2')
	parse('hudtxtcolorfade 0 2 2000 0 128 255')
	print("HT anim")
end
function ht_clear1()
	parse('hudtxt 4 "" 0 0 0')
	print("HT clear1")
end
function ht_clearall()
	parse('hudtxtclear 0')
	print("HT clearall")
end
addhook("join","ht_join")
function ht_join(id)
	timer(3000,"ht_show")
	timer(25000,"ht_anim")
	timer(32000,"ht_clear1")
	timer(36000,"ht_clearall")
end
