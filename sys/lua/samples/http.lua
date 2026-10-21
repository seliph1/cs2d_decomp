--------------------------------------------------
-- HTTP by Unreal Software                      --
-- 01.12.2017 - www.UnrealSoftware.de           --
-- Simple script to get userid from username    --
--------------------------------------------------

if sample==nil then sample={} end
sample.http={}

addhook("httpdata","sample.http.httpdata")

sample.http.username = "DC"
sample.http.http_request = reqhttp("unrealsoftware.de","/connect.php?getid="..sample.http.username, 0)

function sample.http.httpdata(id, state, result)
	if (sample.http.http_request == id) and (state > 0) then
		print(sample.http.username.."'s user ID is "..result)
	else
		print("Failed to receive user ID")
	end
end