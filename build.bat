@echo off
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo ====================================================
echo        Building CS2D Decompilation Project
echo ====================================================

echo [1/2] Compiling test suite (test_cs2d.exe)...
gcc -Wall -Wextra -Iinclude src/cs2d_weapons.c src/cs2d_player.c src/cs2d_map.c src/cs2d_entities.c src/cs2d_fx.c src/cs2d_game.c src/cs2d_network.c src/cs2d_render.c src/cs2d_ui.c src/cs2d_config.c src/cs2d_client.c src/cs2d_scene_lobby.c src/cs2d_scene_game.c src/cs2d_scene_editor.c tests/test_cs2d.c -lSDL3 -lopengl32 -lgdi32 -lws2_32 -lm -o test_cs2d.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile test suite.
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling game client (cs2d_client.exe)...
gcc -Wall -Wextra -Iinclude src/cs2d_weapons.c src/cs2d_player.c src/cs2d_map.c src/cs2d_entities.c src/cs2d_fx.c src/cs2d_game.c src/cs2d_network.c src/cs2d_config.c src/cs2d_render.c src/cs2d_ui.c src/cs2d_client.c src/cs2d_scene_lobby.c src/cs2d_scene_game.c src/cs2d_scene_editor.c src/main_client.c -lSDL3 -lopengl32 -lgdi32 -lws2_32 -lm -o cs2d_client.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to compile game client.
    exit /b %ERRORLEVEL%
)

echo.
echo ====================================================
echo Build Successful!
echo  - test_cs2d.exe  : Full regression test suite
echo  - cs2d_client.exe: Standalone playable game client (SDL3 + OpenGL)
echo ====================================================

