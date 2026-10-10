--------------------------------------------------
-- Cars Script by Unreal Software               --
-- 21.02.2019 - www.UnrealSoftware.de           --
-- Uses physics to create cars                  --
--------------------------------------------------

dofile("sys/lua/physics.lua")

physController = { movement_absolute = 0, movement_relative = 1, movement_vehicle = 2}
physObject = { property_wall = 0, property_obstacle = 1, property_floor = 2}
physImage = { parent_relative = 0, parent_absolute = 1}

addhook("startround","startround")

function startround()
	--parse("phys_spawnvehicle 1 850 250")

	car1(400,450)
	car2(400,350)
end

function car1(x, y)
	local platform = Physics:newRectanglePlatform(x, y, 0, 170, 60)
	platform:setMass(3)
	--platform:setBounciness(1)

	-- Walls/Obstacles (Optimize)
	physObject = Physics:newRectangle(48, -30, 0, 64, 5)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(48, 30, 0, 64, 5)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(-38, -30, 0, 80, 5)
	physObject:setParent(platform)

	--physObject = Physics:newRectangle(6, 0, 5, 0, 60)
	--physObject:setParent(platform)

	physObject = Physics:newRectangle(60, 0, 0, 45, 60)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(-57, 0, 0, 44, 60)
	physObject:setParent(platform)

	-- Wheels
	wheelRL = Physics:newTire(44, 29, 0, 24, 8)
	wheelRL:setParent(platform)

	wheelRR = Physics:newTire(44, -29, 0, 24, 8)
	wheelRR:setParent(platform)

	wheelFL = Physics:newTire(-54, 29, 0, 24, 8)
	wheelFL:setParent(platform)

	wheelFR = Physics:newTire(-54, -29, 0, 24, 8)
	wheelFR:setParent(platform)

	-- Controller Setup
	controller = Physics:newController(-32, 0, 16, platform, Physics.CONTROLLER_VEHICLE)
	controller:setParent(platform)

	controller:set("force_clear")
	controller:set("force_front_up", 700)
	controller:set("force_front_down", 450)

	-- Setup Controller Wheels
	controller:set("add_frontwheel", wheelFL:getId())
	controller:set("add_frontwheel", wheelFR:getId())

	controller:set("add_rearwheel", wheelRL:getId())
	controller:set("add_rearwheel", wheelRR:getId())
	controller:set("steering_sensitivity", 15)
	controller:set("steering_angle", 15)

	-- Images
	img1 = image("incbin::incbindata/vehicles/car2_floor.png", 0, 0, 0)
	img2 = image("incbin::incbindata/vehicles/car2_obstacle.png", 0, 0, 1)
	img3 = image("incbin::incbindata/vehicles/car2_wall.png", 0, 0, 1)

	imagephysicsparent(img1, platform:getId(), physImage.parent_relative)
	imagephysicsparent(img2, platform:getId(), physImage.parent_relative)
	imagephysicsparent(img3, platform:getId(), physImage.parent_relative)

	imageshadow(img1, 1)

	return platform
end

function car2(x, y)
	local platform = Physics:newRectanglePlatform(x, y, 0, 170, 60)
	platform:setMass(3)
	--platform:setBounciness(1)

	-- Walls/Obstacles
	physObject = Physics:newRectangle(48, -30, 0, 64, 5)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(48, 30, 0, 64, 5)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(6, 0, 0, 5, 60)
	physObject:setParent(platform)

	physObject = Physics:newRectangle(-64, 0, 0, 32, 60)
	physObject:setParent(platform)

	-- Wheels
	wheelRL = Physics:newTire(44, 33, 0, 24, 8)
	wheelRL:setParent(platform)

	wheelRR = Physics:newTire(44, -33, 0, 24, 8)
	wheelRR:setParent(platform)

	wheelFL = Physics:newTire(-54, 33, 0, 24, 8)
	wheelFL:setParent(platform)

	wheelFR = Physics:newTire(-54, -33, 0, 24, 8)
	wheelFR:setParent(platform)

	-- Controller Setup
	controller = Physics:newController(-32, 0, 16, platform, Physics.CONTROLLER_VEHICLE)
	controller:setParent(platform)

	controller:set("force_clear")
	controller:set("force_front_up", 500)
	controller:set("force_front_down", 450)

	-- Setup Controller Wheels
	controller:set("add_frontwheel", wheelFL:getId())
	controller:set("add_frontwheel", wheelFR:getId())
	controller:set("steering_sensitivity", 5)

	controller:set("add_rearwheel", wheelRL:getId())
	controller:set("add_rearwheel", wheelRR:getId())
	controller:set("steering_angle", 15)

	-- Images
	img1 = image("incbin::incbindata/vehicles/car_floor.png", 0, 0, 0)
	img2 = image("incbin::incbindata/vehicles/car_obstacle.png", 0, 0, 1)
	img3 = image("incbin::incbindata/vehicles/car_wall.png", 0, 0, 1)

	imagephysicsparent(img1, platform:getId(), physImage.parent_relative)
	imagephysicsparent(img2, platform:getId(), physImage.parent_relative)
	imagephysicsparent(img3, platform:getId(), physImage.parent_relative)

	imageshadow(img1, 1)

	return platform
end

startround()
