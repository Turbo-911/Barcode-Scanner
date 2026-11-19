@echo off
title Chili Mili - Barcode POS Studio
cd /d "%~dp0"

rem ============================================================
rem  ONE-COMMAND BUILD + RUN. Just run:  .\run_gui.bat
rem  Toolchain: MSYS2 UCRT64 (g++ + SFML 3, both from pacman).
rem  No CMake, no manual steps, nothing to edit in this file.
rem  The source list below follows the repository structure:
rem  src split into barcode / camera / common / domain /
rem  persistence / services / ui modules.
rem ============================================================
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

if not exist "C:\msys64\ucrt64\bin\g++.exe" (
    echo [ERROR] MSYS2 g++ not found at C:\msys64\ucrt64\bin\g++.exe
    echo         Install MSYS2, then in the UCRT64 terminal run:
    echo         pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sfml
    pause
    exit /b 1
)

echo Building Chili Mili Barcode POS Studio (one moment)...
"C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -O2 -D_WIN32 -Isrc ^
    src\main.cpp src\App.cpp ^
    src\barcode\encoder.cpp src\barcode\Decoder.cpp ^
    src\camera\camera.cpp ^
    src\common\BMP.cpp src\common\image.cpp src\common\image_io.cpp ^
    src\common\string_utils.cpp src\common\date_time.cpp ^
    src\domain\enums.cpp ^
    src\persistence\record_file.cpp src\persistence\product_serialiser.cpp src\persistence\order_serialiser.cpp ^
    src\persistence\order_detail_serialiser.cpp src\persistence\bill_serialiser.cpp ^
    src\persistence\i_product_repo.cpp src\persistence\i_order_repo.cpp src\persistence\i_bill_repo.cpp ^
    src\services\id_generator.cpp src\services\bill_calculator.cpp src\services\bill_service.cpp ^
    src\services\product_services.cpp src\services\order_service.cpp src\services\scan_service.cpp ^
    src\ui\BarcodeStudioGui.cpp src\ui\Widgets.cpp ^
    -o barcode_studio_gui.exe ^
    -lsfml-graphics -lsfml-window -lsfml-system ^
    -lmf -lmfplat -lmfreadwrite -lmfuuid -lole32 -loleaut32 -lcomdlg32 -luuid

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed - see the compiler messages above.
    pause
    exit /b 1
)

echo [OK] Build succeeded. Starting the GUI...
start "" /d "%~dp0" "barcode_studio_gui.exe"
exit /b 0
