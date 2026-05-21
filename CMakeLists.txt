cmake_minimum_required(VERSION 3.16)

project(dota_relics_catalog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dota_relics_catalog
    src/database.cpp
    src/main.cpp
    src/views.cpp
)
