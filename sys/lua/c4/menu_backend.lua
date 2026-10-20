function init_array(length,mode)
    local array={}
    for i=1,length do
        if type(mode)=="table" then
            local content = {}
            for key,value in pairs(mode) do
                content[key]=value
            end
            array[i]=content
        else
            array[i]=mode 
        end
    end
    return array
end 

function init_doublearray(len1,len2,mode)
    local array = {}
    for x = 0, len1 do
		array[x] = {}
        for y = 0, len2 do
			if type(mode)=="table" then
				local contents ={};
				for key, value in pairs(mode) do
					contents[key]=value;
				end
				array[x][y]=contents;
			else
            	array[x][y] = mode
			end
        end
    end
    return array
end



USER={}
USER.MENU_DIR 		= init_array(32,"")
USER.MENU_DATA		= init_array(32,"")
USER.MENU_PAGE		= init_array(32,0)
USER.MENU_ACTIVE	= init_array(32,false)
USER.MENU_XY		= init_array(32,"");
USER.MENU_AREA		= init_array(32,false);
USER.MENU_COOLDOWN	= init_array(32,0);		-- Anti macro
USER.MENU_WARNING	= init_array(32,0);
USER.TITLE 			= init_array(32,"")



_menu = menu
menu = {}
menu.data={
	["@main_menu"]={
		title="MAIN MENU";
		button={
			{name=""};
			{name=""};
			{name=""};
			{name=""};
			{name=""};
			{name=""};
			{name=""};
			{name=""};
			{name=""};
		}
	};
}
function menu.get(name, page, button, page_size)
	local data={}
	if type(name) == "table" then
		data = name
	else
		data = menu.data[name]
	end
	
	local page_size = page_size or 8
	local slot = page_size * (page - 1) + button
	local item = data.button[ slot ]
	return item, slot
end

function menu.getdirection(x1,y1,x2,y2)
    if x1==x2 and y1==y2 then return 0,"Center" end
    local rot = math.deg(math.atan2(x1-x2,y1-y2)) + 180
    local dist = math.floor( math.sqrt( math.pow(x1 - x2, 2) + math.pow(y1 - y2, 2) ) + 0.5)
    local directions = {"North", "North-West", "West", "South-West", "South", "South-East", "East", "North-East"}
    local index = ( math.floor( (rot / 45) + 0.5 ) % 8 ) + 1

    return dist, directions[index]
end


function menu.progressbar(size, min, max, current, char)
    local char = char or ">"
    local full = max - min
    local position = current - min
    local scale = position / full
    local true_size = math.min(math.max(0, math.ceil( size * scale ) - 2), size-2)
    local str = "#" .. string.rep("=",true_size) .. char
    return str
end

function menu.show(uid,name,page,area,no_renew)

	if area then 
		if not(no_renew) then 
			if not USER.MENU_AREA[uid] then
				USER.MENU_XY[uid] = player(uid,"tilex").."|"..player(uid,"tiley")
			end
		end
		USER.MENU_AREA[uid] = true 
	else
		--if menu.dir_length(uid) == 1 then 
			--local prev_menu = type(name) == "table" and "@main_menu" or name;
			--menu.dir_reset(uid, prev_menu)
		--end
		USER.MENU_AREA[uid] = false;
		USER.MENU_XY[uid] = "";
	end

	v_me=uid
	v_posx = player(uid,"x")
	v_posy = player(uid,"y")
	-- A função de exibir tudo no servidor (mapas, comandos, etc) utiliza a função de indexação de menu inventada por mim (Mozilla)
	-- A estrutura é assim:
	--[[
		data={}					Este é a tabela de dados em que serão retiradas as informações para indexar. 
		
		newdata={
			title="";   		Titulo do menu. Bem simples.
			
			button = {
				{name="",caption="",target="@",func="myfunc(v_me)"};
			};
			
			Aqui os botões são uma tabela individual com 4 elementos:
			# name: nome do botão. É o que será exibido na lista como branco
			
			# caption: descrição do botão. É o que será exibido na lista como cinza.
			
			# target: O botão poderá navegar para outros menus alem desse. Eles são identificador por uma string.
			Da maneira que organizamos os menus no nosso script, os menus tem nome interno com prefixo @<nome>
			
			func: função que é chamada assim que o jogador aperta o botão.
			É possivel passar a variável v_me para função chamada.
			(Isto é algo que eu quero remover, pois v_me é uma variável global.)
			(A ideia é passar os argumentos de forma mais inteligente utilizando a sintaxe do lua do loadstring.)
			
			
			Os nomes e os captions dos botões tambem podem ser definidos dinamicamente por funções.
		}
	--]]
	local menu_page = page or 1
	local menu_name = name
	local data = {}
	
	
	if type(name) == "table" then
		data = name
	elseif type(name) == "string" then
		if name:sub(1,1)== "$"  or name:sub(1,1) == "#" then
			data = menu.convert(name)
			menu_name = data
		else
			data = menu.data[name]
		end
	end	

	if data then
		local title = data.title:decode():gsub("[(),|]","")
		local button = data.button or {}
		local length = math.ceil(#button/ 7)
				
		local content = title
		if #button > 9 then
			local page_size = 7
			if player(uid,"health") > 0 then
				page_size = 8
				length = math.ceil(#button/8)
			end
		
			-- Se tiver menos que 9 itens para exibir, mostra eles de uma vez.
			if menu_page < 1 then 
				menu_page = length 
			elseif menu_page > length then 
				menu_page = 1 
			end
		
			content = "["..menu_page.."/"..length.."] "..title

			
			-- Loop que itera de 8 em 8, pois é a acomodação máxima de itens (considerando os botões de navegação)
			for i = page_size * menu_page - (page_size-1), page_size * menu_page do
				if button[i] then 
					local name=button[i].name:decode():gsub("[@(),|]","")
					-- Condição para o botão estar ativo
					-- Chamada de função q retorna o valor de verdadeiro ou falso e decide se a string deverá estar entre parênteses
					if button[i].disabled or ( button[i].condition and not loadstring("return "..button[i].condition)() )
					then content = content..",("..name..")"
					else content = content..","..name
					end	
					-- Descrição do botão
					-- Faz uma chamada da função na tabela para substituir de forma dinamica o conteúdo da descrição
					if button[i].caption
					then content = content.."|"..button[i].caption:decode():gsub("[(),|]","")
					end	
				else 
					content = content .."," 
				end
			end
			
			if player(uid,"health") > 0 then
				content = content ..",<|>"
			else
				-- Se a pagina for a ultima ou a primeira, acomoda mais um botão.
				if menu_page == length 
				then content = content ..",(NEXT)"
				else content = content ..",>> NEXT >>|".."PAGE "..menu_page+1 
				end
		
				if menu_page == 1 
				then content = content ..",(PREVIOUS)"
				else content = content ..",<< PREVIOUS <<|".."PAGE "..menu_page-1
				end	
			end	

		elseif #button==0 then
			content=content..",(EMPTY)"
		else
			for slot,button in ipairs(button) do
				local name=button.name:decode():gsub("[(),|]","")
				
				
				if button.disabled or ( button.condition and not loadstring("return "..button.condition)() )
				then content = content..",("..name..")"
				else content = content..","..name
				end	
				
				if button.caption
				then content = content.."|"..button.caption:decode():gsub("[(),|]","")
				end
			end
		end
		content=content..","
		
		-- Dados de navegação que são salvos para cada player individualmente.
		v_me=nil
		v_posx = nil
		v_posy = nil
	
		USER.MENU_DATA[uid] = menu_name
		USER.MENU_PAGE[uid] = menu_page
		
		-- Debug que mostra o tamanho da string por exibição de menu
		--print(string.len(content),content)
		
		-- O limite de exibição de caracteres para o menu do CS2D é de 400 caracteres.
		-- Se for maior que 400, o script tentará "enxugar" a string para ficar menor.
		local content_lite = string.gsub(content,"|.-,", function(a)  return "|," end )
		if string.len(content)> 400 then
			content = content_lite
		end	
		local s = content
		
		-- A string é exibida no formato de escape em hexadecimal, para ser exibido pela função UTF-8 do CS2D.
		--[[
		local s = ""
		local status,err = pcall(function()
			s = utf8.convert(content)
			--s=utf8basic(content)
		end)

		if status then
		else
			print(string.char(169).."255000000"..err.." on menu page "..menu_page.." of "..data.title)
			s=content
		end--]]
		--msg("MENU DIR: "..tostring(USER.MENU_DIR[uid]).."| Menu data : "..tostring(USER.MENU_DATA[uid]).."| Menu page: "..tostring(USER.MENU_PAGE[uid]).."| MENU ACTIVE? : "..tostring(USER.MENU_ACTIVE[uid]))
		--msg("\169000255000AREA MENU? : "..tostring(USER.MENU_AREA[uid]))
		--msg("\169150150150MENU XY : "..USER.MENU_XY[uid].." __ \169230230230 CURRENT XY : "..player(uid,"tilex").."|"..player(uid,"tiley"))
		USER.MENU_ACTIVE[uid] = true
		_menu(uid,s)
		
	end
end

function menu.buttonside(uid, size)
    local sw, sh = player(uid,"screenw"), player(uid,"screenh")
    local mx, my = player(uid,"mousex"), player(uid,"mousey")

    local scalew = 850
    local scaleh = 480
    
    local fx = sw/scalew
    local fy = sh/scaleh
    
    if size=="small" then
        local scale = math.min(math.max( (mx*fx - 150)/100, -1), 1)
        if mx*fx > 150 then
            return "right", scale
        else
            return "left", scale
        end    
    elseif size=="big" then
        local scale = math.min(math.max( (mx*fx - sw/2)/260, -1), 1)
        if mx*fx > sw/2 then
            return "right", scale
        else
            return "left", scale
        end    
    end    

    --local menusizex = 272 -- same size in every resolution
    --local menusizey = 440
    
    --local menuposx = 20 -- always offsets by 20 pixels and align vertically in every resolution....
    --local menuposy = 20
end


function menu.erase(uid)
	_menu(uid,"")
end

function menu.decode(s, ...)
    local args = {...};
    args = #args == 1 and type(args[1]) == "table" and args[1] or args;
    -- return true if there was an expansion
    local function DoExpand(iscode)
        local was = false;
        local mask = iscode and "()%$(%b{})" or "()%$([%a%d_]*)";
        local drepl = iscode and "_$" or "_$"; -- '$' 대신 '_$' 사용
        s = s:gsub(mask, function(pos, code)
            if s:sub(pos - 1, pos - 1) == "_" then return "$"..code; -- 이스케이프 문자 '_' 처리
            else
                was = true; local v, err;
                if iscode then code = code:sub(2, -2);
                else
                    local n = tonumber(code);
                    if n then v = args[n]; end;
                end;
                if not v then
                    v, err = loadstring("return "..code);
                    if not v then error(err); end;
                    v = v();
                end;
                if v == nil then v = ""; end;
                v = tostring(v):gsub("%$", drepl);
                return v;
            end;
        end);
        if not (iscode or was) then s = s:gsub("_%$", "$"); end; -- 원래의 '$' 복원
        return was;
    end
    repeat DoExpand(true); until not DoExpand(false);
    return s;
end
string.decode = menu.decode

function string.explode(t,m)
	local m=m or "%s"
	local tbl = {}
	m = "[^"..m.."]+"
	for w in string.gmatch(t,m) do
		table.insert(tbl,w)
	end
	return tbl
end

function menu.convert(tbl)
	local data=tbl
	if data:sub(1,1)=="#" then
		data = data:match("#(.+)")
		if data:find("table:") then
			data = table.show(data)
		else
			data = _G[data]
		end		
	elseif data:sub(1,1)=="$" then
		data = loadstring("return "..data:match("$(.+)"))()
		return data
	end
	
	local newdata={
		title=tostring(tbl).."@b";
		button={
			--{name="Menu Principal",caption="Voltar",target="@main_menu"}
		};
	};

	for k,v in pairs(data) do
		if tostring(v) ~= tostring(data) then 
		if type(k)=="number" then
			if type(v)=="table" then
				table.insert(newdata.button,{name=tostring(v),caption="ID: "..tostring(k),target="#"..tostring(v)})
			elseif type(v)=="number" or type(v)=="string" then	
				table.insert(newdata.button,{name="Value: "..v,caption="ID: "..k,disabled=true})
			end
		else
			if type(v)=="table" then
				table.insert(newdata.button,{name=tostring(k),caption=tostring(v),target="#"..tostring(v)})
			elseif type(v)=="function" then	
				table.insert(newdata.button,{name=tostring(k),caption=tostring(v),disabled=true})
			else
				table.insert(newdata.button,{name=tostring(k),caption=type(v)..": "..tostring(v),disabled=true})
			end
		end	
		end	
	end
	return newdata
end

function menu.active(uid)
	return USER.MENU_ACTIVE[uid]
end

function menu.dir_add(uid,target)
	USER.MENU_DIR[uid]=USER.MENU_DIR[uid]..">"..target
end

function menu.dir_remove(uid)
	local dir=USER.MENU_DIR[uid]:explode(">")
	table.remove(dir)
	USER.MENU_DIR[uid]=table.concat(dir,">")
end

function menu.dir_get(uid,order)
	local order=order or 1
	local dir=USER.MENU_DIR[uid]:explode(">")
	return dir[#dir-(order-1)]
end

function menu.dir_length(uid)
	local dir=USER.MENU_DIR[uid]:explode(">")
	return #dir
end

function menu.dir_reset(uid,custom)
	if custom then
		USER.MENU_XY[uid] = player(uid,"tilex").."|"..player(uid,"tiley")
		USER.MENU_DIR[uid]=custom
	else
		USER.MENU_DIR[uid]="@main_menu"
		--[[
		if p_level[uid] > 0 then
			user_dir[uid]=".root"
		else
			user_dir[uid]="@public_panel"
		end	
		--]]
	end	
end

function menu.create(nameid,title)
	menu.data[nameid]={
		title=title;
		button={
		};
	};
end

function menu.remove(nameid)
	menu.data[nameid]=nil
end

function menu.addbutton(nameid,button,index)
	local index=index or #menu.data[nameid].button+1
	table.insert(menu.data[nameid].button,index,button)
end
menu.insertbutton=menu.addbutton

function menu.removebutton(nameid,index)
	local index = index or #menu.data[nameid].button
	table.remove(menu.data[nameid].button,index)
end

function menu.addspace(nameid,index)
	local index=index or #menu.data[nameid].button+1
	table.insert(menu.data[nameid].button,index,{name=""})
end

function menu.player_command(uid,command)
	local data=player(0,"table")
	local newdata={
		title="COMMANDS",
		button={
				--{name="Comandos",caption="Voltar",target="@control_commands"};
			},
		}
	for key,tid in pairs(data) do
		newdata.button[#newdata.button+1]={name=player(uid,"name"),func="command_init("..uid..", ' "..command.." "..tid.." ' )"}
	end
	menu.show(uid,newdata)
end

function menu.switch_command(uid,command)
	-- Ler a linha 24 deste script para instruções.
	local newdata={
		title=command;
		button={
				{name=command,caption="ACTIVATE",func="command_init("..uid..",'"..command.." on')"};
				{name=command,caption="DEACTIVATE",func="command_init("..uid..",'"..command.." off')"};
			}
		}	
	menu.show(uid,newdata)	
end

function menu.custom_command(uid,command)
	-- Ler a linha 24 deste script para instruções.
	local data=command
	local newdata={
		{name="COMMANDS",caption="Voltar",target="@control_commands"};
	}
end

addhook("menu","menu.hook");													
addhook("serveraction","menu.input");	


function menu.hook(uid,title,button)
	if button==0 then 	
		menu.dir_remove(uid)
		if menu.dir_length(uid) > 0 then
			menu.show(uid,menu.dir_get(uid,1))
		else
			USER.MENU_ACTIVE[uid] = false
			USER.MENU_AREA[uid] = false;
			USER.MENU_XY[uid] = "";
		end
		return 
	end
	
	local menu_data = USER.MENU_DATA[uid]
	local menu_page = USER.MENU_PAGE[uid]
	local menu_title = ""
	local menu_button = 0
	local page_size = 7
	if player(uid,"health") > 0 then
		page_size = 8
	end
	
	if type(menu_data) == "table" then
		menu_button = #menu_data.button
		menu_title = menu_data.title
	else 
		menu_button = #menu.data[menu_data].button
		menu_title = menu.data[menu_data].title
	end
	
	if menu_button > 9 then
			
		if page_size == 7 then
			if button==9 then menu.show(uid, menu_data, menu_page - 1) return end
			if button==8 then menu.show(uid, menu_data, menu_page + 1) return end
		end
	
		if button==9 then 
			local align = menu_title : find("@b") and "big" or "small"
			local side = menu.buttonside(uid,align)
			if side == "left" then 
				menu.show(uid, menu_data, menu_page - 1) 
			elseif side=="right" then	
				menu.show(uid, menu_data, menu_page + 1) 
			end	
			return 
		end
	end
	

	local item,slot = menu.get(menu_data, menu_page, button, page_size)
	
	if item.func then 
		local func_exec;
		if not USER.MENU_AREA[uid] then 
			func_exec = true;
		else
			if USER.MENU_XY[uid] ~= "" then 
				local mx, my = USER.MENU_XY[uid]:match("(%d+)|(%d+)")
				local px, py = player(uid,"tilex"), player(uid,"tiley")
				if px >= mx-1 and px <= mx+1 and py >= my-1 and py <= my+1 then 
					func_exec = true;
				end
			end
		end

		if func_exec then 
			v_me=uid
			v_posx=player(uid,"x") 
			v_posy=player(uid,"y")
			v_slot=slot
			
			if not ( item.disabled or (item.condition and not loadstring("return "..item.condition)() ) )  then
				if USER.MENU_ACTIVE[uid] then
					USER.MENU_ACTIVE[uid]=false
					
					assert(loadstring(item.func))()
				end	
			end
			
			v_me=nil
			v_posx=nil
			v_posy=nil
			v_slot=nil
		end
	end

	if item.target then
		if item.target=="self" then
			local con = true
			if USER.MENU_AREA[uid] then 
				local mx, my = USER.MENU_XY[uid]:match("(%d+)|(%d+)")
				local px, py = player(uid,"tilex"), player(uid,"tiley")
				if not(px >= mx-1 and px <= mx+1 and py >= my-1 and py <= my+1) then 
					con = false;
				end
			end

			if con then 
				menu.show(uid,menu.dir_get(uid,1),menu_page,USER.MENU_AREA[uid],true)
			end
		elseif item.target=="back" then
			menu.dir_remove(uid)
			menu.show(uid,menu.dir_get(uid,1))
		elseif item.target=="exit"	then
			menu.dir_reset(uid)
			USER.MENU_ACTIVE[uid] = false
		else
			local con = true
			if USER.MENU_AREA[uid] then 
				local mx, my = USER.MENU_XY[uid]:match("(%d+)|(%d+)")
				local px, py = player(uid,"tilex"), player(uid,"tiley")
				if not(px >= mx-1 and px <= mx+1 and py >= my-1 and py <= my+1) then 
					con = false;
				end
			end

			if con then 
				--msg("\169100100255item.target call, player area: "..tostring(USER.MENU_AREA[uid]))
				menu.dir_add(uid,item.target)
				menu.show(uid,item.target,false,USER.MENU_AREA[uid],true)
			end
		end		
	end	
end

function menu.input(uid,action)
	menu.dir_reset(uid)
	
	if action==1 then
		--if USER.LEVEL[uid] >= 2 and USER.LEVEL[uid] < 4 then
			--menu.dir_reset(uid,"@main_vip")
			--menu.show(uid,"@main_vip");
		--else
			--if USER.LEVEL[uid] >= 4 then
				--menu.dir_reset(uid,"@main_menu")
				--menu.dir_add(uid,"@main_admin")
				--menu.show(uid,"@main_admin");
			--else
				menu.show(uid,"@main_menu");
			--end
		--end
		return
	end
end
