@echo off
chcp 65001 >nul
setlocal
set PATH=D:\mingw64\bin;%PATH%
cd /d "%~dp0"
if not exist build mkdir build

set CORE=src\model.cpp src\store.cpp src\demo_seed.cpp src\user_service.cpp src\item_service.cpp src\search_service.cpp src\reservation_service.cpp src\report_service.cpp src\log_stats_service.cpp

echo [1/3] 编译命令行版...
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -static -Iinclude ^
    %CORE% src\cli.cpp src\main.cpp ^
    -o build\campus_market.exe
if errorlevel 1 goto :fail

echo [2/3] 编译单元测试...
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -static -Iinclude ^
    %CORE% tests\tests.cpp ^
    -o build\campus_tests.exe
if errorlevel 1 goto :fail

echo [3/3] 编译 Web 版...
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -static -Iinclude -Isrc ^
    %CORE% src\web\web_server.cpp src\web\main_web.cpp ^
    -o build\campus_market_web.exe -lws2_32 -lshell32
if errorlevel 1 goto :fail

echo.
echo 编译完成：
echo   build\campus_market.exe      命令行版
echo   build\campus_tests.exe       单元测试
echo   build\campus_market_web.exe  图形界面（启动后手动访问 http://127.0.0.1:8765/）
exit /b 0

:fail
echo.
echo 编译失败。
exit /b 1
