cmake_minimum_required(VERSION 3.16)
project(BarcodeScannerChiliMili CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SFML 3 — if CMake cannot find it, pass your install location:
#   cmake -B build -DSFML_DIR="C:/msys64/ucrt64/lib/cmake/SFML"
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(barcode_studio_gui
    src/main.cpp
    src/App.cpp
    src/barcode/encoder.cpp
    src/barcode/Decoder.cpp
    src/camera/camera.cpp
    src/common/BMP.cpp
    src/common/image.cpp
    src/common/image_io.cpp
    src/common/string_utils.cpp
    src/common/date_time.cpp
    src/domain/enums.cpp
    src/persistence/record_file.cpp
    src/persistence/product_serialiser.cpp
    src/persistence/order_serialiser.cpp
    src/persistence/order_detail_serialiser.cpp
    src/persistence/bill_serialiser.cpp
    src/persistence/i_product_repo.cpp
    src/persistence/i_order_repo.cpp
    src/persistence/i_bill_repo.cpp
    src/services/id_generator.cpp
    src/services/bill_calculator.cpp
    src/services/bill_service.cpp
    src/services/product_services.cpp
    src/services/order_service.cpp
    src/services/scan_service.cpp
    src/ui/BarcodeStudioGui.cpp
    src/ui/Widgets.cpp
)
target_include_directories(barcode_studio_gui PRIVATE src)
target_link_libraries(barcode_studio_gui PRIVATE SFML::Graphics SFML::Window SFML::System)

if(WIN32)
    # Media Foundation (camera) + common dialog (Browse button)
    target_link_libraries(barcode_studio_gui PRIVATE
        mf mfplat mfreadwrite mfuuid ole32 oleaut32 comdlg32)
endif()

# The GUI loads its bundled font from assets/ next to the executable.
add_custom_command(TARGET barcode_studio_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:barcode_studio_gui>/assets)
