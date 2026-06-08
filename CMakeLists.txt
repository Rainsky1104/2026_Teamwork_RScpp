set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "C:/msys64/ucrt64")
cmake_minimum_required(VERSION 3.16)

project(RemoteSensingQtStarter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

# 关键修改：OpenCV 设为 REQUIRED，并添加 stitching 组件
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d stitching)
find_package(GDAL QUIET)

add_executable(RemoteSensingQtStarter WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/Algorithms.cpp
    src/RasterIO.cpp
    src/RasterRenderDialog.cpp
    include/rs/DataObject.h
    include/rs/Geometry.h
    include/rs/LayerManager.h
    include/rs/ProcessingAlgorithm.h
    include/rs/Algorithms.h
    include/rs/RasterIO.h
    include/rs/RasterRenderDialog.h
    include/rs/MainWindow.h
)

target_include_directories(RemoteSensingQtStarter PRIVATE include)
target_link_libraries(RemoteSensingQtStarter PRIVATE ${QT_PACKAGE}::Widgets)

# OpenCV 链接（会自动包含所有已找到的组件库）
if(OpenCV_FOUND)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_OPENCV=1)
    target_link_libraries(RemoteSensingQtStarter PRIVATE ${OpenCV_LIBS})
endif()

if(GDAL_FOUND)
    target_compile_definitions(RemoteSensingQtStarter PRIVATE RS_WITH_GDAL=1)
    target_link_libraries(RemoteSensingQtStarter PRIVATE GDAL::GDAL)
endif()

if(MSVC)
    target_compile_options(RemoteSensingQtStarter PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(RemoteSensingQtStarter PRIVATE -Wall -Wextra -Wpedantic)
endif()