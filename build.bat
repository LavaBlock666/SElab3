@echo off
chcp 65001 >nul
setlocal
set PATH=D:\mingw64\bin;%PATH%
cd /d "%~dp0"
if not exist build mkdir build

echo [1/2] 编译主程序...
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude ^
    src\model.cpp src\store.cpp src\user_service.cpp src\item_service.cpp ^
    src\search_service.cpp src\reservation_service.cpp src\report_service.cpp ^
    src\log_stats_service.cpp src\cli.cpp src\main.cpp ^
    -o build\campus_market.exe
if errorlevel 1 goto :fail

echo [2/2] 编译单元测试...
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude ^
    src\model.cpp src\store.cpp src\user_service.cpp src\item_service.cpp ^
    src\search_service.cpp src\reservation_service.cpp src\report_service.cpp ^
    src\log_stats_service.cpp tests\tests.cpp ^
    -o build\campus_tests.exe
if errorlevel 1 goto :fail

echo.
echo 编译完成：
echo   build\campus_market.exe
echo   build\campus_tests.exe
exit /b 0

:fail
echo.
echo 编译失败。
exit /b 1
