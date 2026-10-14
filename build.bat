@echo off
setlocal
set VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
set MH=third_party\minhook\src
set IG=third_party\imgui
cl /nologo /O2 /MT /W3 /c /Ithird_party\minhook\include /Fobuild\ %MH%\hook.c %MH%\buffer.c %MH%\trampoline.c %MH%\hde\hde64.c || exit /b 1
rem Dear ImGui (in-game settings panel, overlay.hpp). NDEBUG: its asserts would embed build-machine source paths
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /c /DNDEBUG /DUNICODE /D_UNICODE /I%IG% /I%IG%\backends /Fobuild\ ^
   %IG%\imgui.cpp %IG%\imgui_draw.cpp %IG%\imgui_tables.cpp %IG%\imgui_widgets.cpp %IG%\backends\imgui_impl_dx11.cpp %IG%\backends\imgui_impl_win32.cpp || exit /b 1
set OBJS=build\hook.obj build\buffer.obj build\trampoline.obj build\hde64.obj build\imgui.obj build\imgui_draw.obj build\imgui_tables.obj build\imgui_widgets.obj build\imgui_impl_dx11.obj build\imgui_impl_win32.obj
set INC=/Ithird_party\minhook\include /I%IG% /I%IG%\backends
set LIBS=user32.lib winmm.lib bcrypt.lib d3d11.lib d3dcompiler.lib gdi32.lib dwmapi.lib
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX %INC% ^
   src\main.cpp src\proxy_version.cpp %OBJS% ^
   /Fobuild\ /Fdbuild\ /Zi /LD /Fe:build\version.dll ^
   /link /DEF:src\version.def /DEBUG /OPT:REF /OPT:ICF %LIBS% || exit /b 1
echo built build\version.dll
rem public build (T7_PUBLIC): AI + learning + settings panel only, no recorders/traces/dev hotkeys, no debug symbols
if not exist build\public mkdir build\public
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /DNDEBUG /DT7_PUBLIC /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX %INC% ^
   src\main.cpp src\proxy_version.cpp %OBJS% ^
   /Fobuild\public\ /LD /Fe:build\public\version.dll ^
   /link /DEF:src\version.def /OPT:REF /OPT:ICF %LIBS% || exit /b 1
echo built build\public\version.dll
