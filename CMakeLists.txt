cmake_minimum_required(VERSION 3.21)

project(vrpProxy VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(vrpProxy
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ProxyConnection.cpp
    src/ProxyConnection.h
    src/ProxyServer.cpp
    src/ProxyServer.h
    app.rc
    resources.qrc
)

target_link_libraries(vrpProxy PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    target_compile_definitions(vrpProxy PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(vrpProxy PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
