cmake_minimum_required(VERSION 3.16)
project(CheckOnlineBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(CheckOnlineBot
    main.cpp
    config.h
    telegramClient.h
    telegramClient.cpp
    steamApi.h
    steamApi.cpp
    botManager.h
    botManager.cpp
)

target_link_libraries(CheckOnlineBot Qt6::Core Qt6::Network)