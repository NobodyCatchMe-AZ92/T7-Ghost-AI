@echo off
rem Builds the two shareable harvesters (read-only recorders, no hooks):
rem   build\harvest_replay\dinput8.dll  -> Win64\t7GhostReplayData\
rem   build\harvest_live\dinput8.dll    -> Win64\t7GhostLIVEplayData\
rem (dinput8.dll since 2026-10-23; they still work renamed to version.dll)
setlocal
set VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
for %%V in (replay live) do (
  if not exist build\harvest_%%V mkdir build\harvest_%%V
)
set CF=/nologo /std:c++20 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set LF=/link /DEF:src\version.def /OPT:REF /OPT:ICF winmm.lib bcrypt.lib user32.lib gdi32.lib
cl %CF% src\harvest.cpp src\proxy_version.cpp /Fobuild\harvest_replay\ /LD /Fe:build\harvest_replay\dinput8.dll %LF% || exit /b 1
cl %CF% /DT7_LIVE src\harvest.cpp src\proxy_version.cpp /Fobuild\harvest_live\ /LD /Fe:build\harvest_live\dinput8.dll %LF% || exit /b 1
echo built build\harvest_replay\dinput8.dll and build\harvest_live\dinput8.dll
