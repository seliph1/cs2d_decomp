-- Records the Lua bind list the original dedicated announces in [252,6] / [224].
local names = {
    "Q", "E", "A", "Z", "0", "9",
    "f1", "f12", "F1",
    "mouse1", "mouse2", "mouse3", "mwheelup", "mwheeldown",
    "leftshift", "rightshift", "leftctrl", "rightctrl",
    "leftalt", "rightalt", "leftsys", "rightsys",
    "leftarrow", "rightarrow", "uparrow", "downarrow",
    "backspace", "tab", "clear", "enter", "escape", "space",
    "pgup", "pgdn", "end", "home", "select", "print", "execute",
    "screen", "ins", "del",
    "kp_5", "kp_leftarrow", "kp_home", "kp_ins", "kp_enter",
}
for _, name in ipairs(names) do
    local ok = addbind(name)
    print("BINDRESULT "..name.." "..tostring(ok))
end

function bind_key_probe(id, key, state)
    print("KEYBIND "..id.." "..key.." "..state)
end
addhook("key", "bind_key_probe")
