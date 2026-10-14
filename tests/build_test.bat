@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /std:c++20 /O2 /EHsc /DWIN32_LEAN_AND_MEAN /DNOMINMAX profile_test.cpp /Fo..\build\ /Fe:..\build\profile_test.exe >nul || exit /b 1
