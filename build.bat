@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"
title FLEY AI C++ - сборка
echo ==========================================
echo   FLEY AI C++ - сборка в EXE одним нажатием
echo ==========================================
echo.
if not exist dist mkdir dist
if not exist build mkdir build

call :findgcc
where g++ >nul 2>nul && goto build_gcc
where cl >nul 2>nul && goto build_msvc

echo Компилятор C++ не найден. Ставлю MinGW-w64 через winget, это один раз...
where winget >nul 2>nul || goto nocompiler
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT --accept-source-agreements --accept-package-agreements
call :findgcc
where g++ >nul 2>nul && goto build_gcc
goto nocompiler

:build_gcc
echo Компилирую через g++ ...
g++ -std=c++17 -O2 -s -mwindows -static -static-libgcc -static-libstdc++ -o "dist\FLEY AI.exe" src\main.cpp -lwinhttp -lwinmm -lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -ladvapi32
if errorlevel 1 goto fail
goto finish

:build_msvc
echo Компилирую через Visual C++ ...
cl /nologo /EHsc /O2 /std:c++17 /utf-8 /DUNICODE /D_UNICODE /Fobuild\ /Fe"dist\FLEY AI.exe" src\main.cpp /link /SUBSYSTEM:WINDOWS
if errorlevel 1 goto fail
goto finish

:finish
rem Существующий dist\.env НЕ перезаписываем (там ваш ключ)
if not exist "dist\.env" if exist ".env" copy /y ".env" "dist\.env" >nul
if not exist "dist\.env" copy /y ".env.example" "dist\.env" >nul
echo.
echo ==========================================
echo   ГОТОВО!  Программа: dist\FLEY AI.exe
echo ==========================================
explorer "dist"
pause
exit /b 0

:nocompiler
echo.
echo ОШИБКА: не удалось найти или установить компилятор.
echo Установите вручную одним из способов и запустите build.bat снова:
echo   1. В командной строке: winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
echo   2. Или Visual Studio Build Tools с компонентом Desktop development with C++
pause
exit /b 1

:fail
echo.
echo ОШИБКА компиляции. Скопируйте текст ошибки выше и пришлите мне.
pause
exit /b 1

:findgcc
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do if exist "%%D\mingw64\bin\g++.exe" set "PATH=%%D\mingw64\bin;%PATH%"
exit /b 0
