addhook("buy","pd_buy")
function pd_buy(id,w) print("PD buy "..w) end
function pd1() print("PD setmoney") parse("setmoney 1 5000") end
function pd2() print("PD sethealth") parse("sethealth 1 50") end
function pd3() print("PD setarmor") parse("setarmor 1 80") end
function pd4() print("PD equip he+kevlarhelm") parse("equip 1 51") parse("equip 1 58") end
function pd5() print("PD strip usp") parse("strip 1 1") end
function pd6() print("PD setmaxhealth") parse("setmaxhealth 1 150") end
timer(12000,"pd1") timer(12500,"pd2") timer(13000,"pd3") timer(13500,"pd4") timer(14000,"pd5") timer(14500,"pd6")
