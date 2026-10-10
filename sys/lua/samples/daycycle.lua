--------------------------------------------------
-- Day Night Cycle Script by Unreal Software    --
-- 28.09.2017 - www.UnrealSoftware.de           --
-- Adds Day and Night cycle to your server      --
--------------------------------------------------

if sample==nil then sample={} end
sample.daynight={}

sample.daynight.secondsPerHour = 1
sample.daynight.seconds = 0

addhook("startround_prespawn", "sample.daynight.load")
function sample.daynight.load()
	
	sample.daynight.clockImage = image("gfx/sprites/clock.png", 180, 40, 2)
	sample.daynight.arrowImage = image("gfx/sprites/clock_arrow.png", 180, 40, 2)
end

sample.daynight.load()

function sample.daynight.setDay()

	sample.daynight.seconds = sample.daynight.secondsPerHour * 12
	
end

function sample.daynight.setNight()
	
	sample.daynight.seconds = 0
	
end

function sample.daynight.setHour(hour)
	
	sample.daynight.seconds = hour * sample.daynight.secondsPerHour + ( sample.daynight.seconds % sample.daynight.secondsPerHour )
	
end

function sample.daynight.setMinute(minutes)

	local hourSeconds = math.floor( sample.daynight.seconds / sample.daynight.secondsPerHour ) * sample.daynight.secondsPerHour
	local minuteSeconds = math.floor( minutes * sample.daynight.secondsPerHour / 60 )
	
	sample.daynight.seconds = hourSeconds + minuteSeconds
	
end

function sample.daynight.getHour()
	
	return math.floor( sample.daynight.seconds / sample.daynight.secondsPerHour )
	
end

function sample.daynight.getMinute()

	return math.floor( sample.daynight.seconds % sample.daynight.secondsPerHour ) * 60 / sample.daynight.secondsPerHour
	
end

function sample.daynight.getTime()

	local hour = tostring(sample.daynight.getHour())
	local minute = tostring(sample.daynight.getMinute())
	
	if #hour == 1 then
	
		hour = "0" .. hour
		
	end
	
	if #minute == 1 then
		
		minute = "0" .. minute
		
	end
	
	return hour .. ":" .. minute
	
end

addhook("second","sample.daynight.cicle")
function sample.daynight.cicle()
	
	sample.daynight.seconds = ( sample.daynight.seconds + 1 ) % ( sample.daynight.secondsPerHour * 24 )

	local hour = ( sample.daynight.seconds / sample.daynight.secondsPerHour + 5 ) % 24
	local percentage = hour / 24
	local time = percentage * 360
	
	parse("sv_daylighttime " .. math.floor( time % 360 ))
	
	tween_rotate(sample.daynight.arrowImage, 1000, math.floor( ( ( ( time - 76 ) * 2 ) % 360) ) )
	
end