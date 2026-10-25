@echo off
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo ====================================================
echo        Building CS2D Decompilation Project
echo ====================================================

echo [1/3] Compiling test suite (test_cs2d.exe)...
gcc -Wall -Wextra -Iinclude -Ithird_party -Ithird_party/lua51/src src/cs2d_weapons.c src/cs2d_player.c src/cs2d_map.c src/cs2d_customtile.c src/cs2d_entities.c src/cs2d_dynobj.c src/cs2d_projectile.c src/cs2d_fx.c src/cs2d_gen.c src/cs2d_tilefx.c src/cs2d_spectate.c src/cs2d_game.c src/cs2d_network.c src/cs2d_random.c src/cs2d_serverlist.c src/cs2d_transfer.c src/cs2d_editor.c src/cs2d_image.c src/cs2d_atlas.c src/cs2d_radar.c src/cs2d_hudtxt.c src/cs2d_hostage.c src/cs2d_render.c src/cs2d_font.c src/cs2d_light.c src/cs2d_gl.c src/cs2d_ui.c src/cs2d_hudmen.c src/cs2d_sound.c src/cs2d_scoreboard.c src/cs2d_config.c src/cs2d_bind.c src/cs2d_platform.c src/cs2d_client.c src/cs2d_scene_lobby.c src/cs2d_scene_game.c src/cs2d_scene_editor.c src/cs2d_server.c src/cs2d_sv_game.c src/cs2d_sv_lua.c src/cs2d_sv_ai.c third_party/lua51/lua_all.c tests/test_cs2d.c -lSDL3 -lopengl32 -lws2_32 -l:libz.a -lm -o test_cs2d.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile test suite.
    exit /b %ERRORLEVEL%
)

echo [2/3] Compiling game client (cs2d_client.exe)...
gcc -Wall -Wextra -Iinclude -Ithird_party src/cs2d_weapons.c src/cs2d_player.c src/cs2d_map.c src/cs2d_customtile.c src/cs2d_entities.c src/cs2d_dynobj.c src/cs2d_projectile.c src/cs2d_fx.c src/cs2d_gen.c src/cs2d_tilefx.c src/cs2d_spectate.c src/cs2d_game.c src/cs2d_network.c src/cs2d_random.c src/cs2d_serverlist.c src/cs2d_transfer.c src/cs2d_editor.c src/cs2d_config.c src/cs2d_bind.c src/cs2d_platform.c src/cs2d_image.c src/cs2d_atlas.c src/cs2d_radar.c src/cs2d_hudtxt.c src/cs2d_hostage.c src/cs2d_render.c src/cs2d_font.c src/cs2d_light.c src/cs2d_gl.c src/cs2d_ui.c src/cs2d_hudmen.c src/cs2d_sound.c src/cs2d_scoreboard.c src/cs2d_client.c src/cs2d_scene_lobby.c src/cs2d_scene_game.c src/cs2d_scene_editor.c src/main_client.c -lSDL3 -lopengl32 -lws2_32 -l:libz.a -lm -o cs2d_client.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile game client.
    exit /b %ERRORLEVEL%
)

echo [3/3] Compiling dedicated server (cs2d_server.exe)...
gcc -Wall -Wextra -Iinclude -Ithird_party -Ithird_party/lua51/src src/cs2d_server.c src/cs2d_sv_game.c src/cs2d_sv_lua.c src/cs2d_sv_ai.c src/cs2d_sv_stubs.c src/cs2d_weapons.c src/cs2d_random.c src/cs2d_network.c src/cs2d_map.c src/cs2d_dynobj.c src/cs2d_entities.c src/cs2d_customtile.c src/cs2d_bind.c src/cs2d_platform.c src/cs2d_image.c third_party/lua51/lua_all.c src/main_dedicated.c -lws2_32 -lwinmm -l:libz.a -lm -o cs2d_server.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile dedicated server.
    exit /b %ERRORLEVEL%
)

echo.
echo ====================================================
echo Build Successful!
echo  - test_cs2d.exe  : Full regression test suite
echo  - cs2d_client.exe: Standalone playable game client (SDL3 + OpenGL)
echo  - cs2d_server.exe: Dedicated server (port of cs2d_dedicated.exe)
echo ====================================================

