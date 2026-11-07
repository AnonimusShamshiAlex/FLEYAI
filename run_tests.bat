@echo off
chcp 65001 >nul
cd /d "%~dp0"
g++ -std=c++17 -O1 -o build_test.exe tests\test_core.cpp && build_test.exe
del build_test.exe >nul 2>nul
pause
