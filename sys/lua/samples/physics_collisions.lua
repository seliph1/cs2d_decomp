

--addhook("oncollision","onCollision")
function onCollision(bodyA, bodyB)
	print("onCollision "..bodyA.." "..bodyB)
end

addhook("oncollisionhit","onCollisionHit")
function onCollisionHit(bodyA, bodyB, velocity)
	print("onCollisionHit "..bodyA.." "..bodyB.." "..velocity)
end 

addhook("onhit","onHit")
function onHit(physID, player, weapon, sx, sy, dmg, dynamicObject)
	print("onHit "..physID.." "..player.." "..weapon.." "..sx.." "..sy.." "..dmg.." "..dynamicObject)
end 






