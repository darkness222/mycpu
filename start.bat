@echo off
cd /d "%~dp0"

echo =============================================
echo           MyCPU Startup Script
echo =============================================
echo.

echo [1/5] Building project...
if not exist "build" mkdir build
cd build

set CMAKE_PATH=D:\Code\Project\mycpufinal\mycpu\cmake\data\bin\cmake.exe
where cmake >nul 2>&1
if errorlevel 1 (
    if exist "%CMAKE_PATH%" (
        set CMAKE_EXE=%CMAKE_PATH%
    ) else (
        echo [ERROR] CMake not found
        pause
        exit /b 1
    )
) else (
    for /f "delims=" %%i in ('where cmake') do set CMAKE_EXE=%%i
)

"%CMAKE_EXE%" -G "MinGW Makefiles" .. || (
    echo [ERROR] CMake configuration failed
    pause
    exit /b 1
)

if defined NUMBER_OF_PROCESSORS (
    set MAKE_JOBS=-j %NUMBER_OF_PROCESSORS%
) else (
    set MAKE_JOBS=-j 2
)

mingw32-make %MAKE_JOBS% || (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

cd ..
echo [OK] Build complete

echo.
echo [2/5] Checking backend executable...
if not exist "build\myCPU.exe" (
    echo [ERROR] build\myCPU.exe not found
    pause
    exit /b 1
)
echo [OK] Backend executable found

echo.
echo [3/5] Checking frontend dependencies...
if not exist "web\node_modules" (
    echo [WARNING] node_modules not found, installing...
    cd web
    call npm install
    cd ..
)
echo [OK] Frontend dependencies ready

echo.
echo [4/5] Starting backend service (port 18080)...
start "MyCPU-Backend" cmd /k "build\myCPU.exe --server"
echo [OK] Backend service started

echo.
echo [5/5] Starting frontend service (port 3000)...
start "MyCPU-Frontend" cmd /k "cd /d ""%~dp0web"" && npm run dev"
echo [OK] Frontend service started

echo.
echo =============================================
echo   All services started!
echo   Frontend: http://localhost:3000
echo   Backend: http://localhost:18080
echo =============================================
echo.
echo Opening browser in 3 seconds...
timeout /t 3 /nobreak >nul
start "" "http://localhost:3000"
echo.
echo Press any key to exit (services will continue running)...
pause >nul
