cmake_minimum_required(VERSION 3.20)

project(MemoryGuardian LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(MemoryGuardian WIN32
    src/main.cpp
    app.rc
)

target_link_libraries(MemoryGuardian PRIVATE
    Qt6::Widgets
    Qt6::Network
    psapi
    shell32
)
