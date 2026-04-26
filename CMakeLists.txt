cmake_minimum_required(VERSION 3.16)
project(CheckOnlineBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ==================== BUILD OPTIONS ====================
option(USE_GUI_CHARTS "Enable QImage/QPainter chart generation" OFF)

# ==================== DEPENDENCIES ====================
if(USE_GUI_CHARTS)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)
    target_compile_definitions(CheckOnlineBot PRIVATE USE_GUI_CHARTS)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
endif()

# ==================== EXECUTABLE ====================
add_executable(CheckOnlineBot
    main.cpp
    config.h
    telegramClient.h
    telegramClient.cpp
    steamApi.h
    steamApi.cpp
    botManager.h
    botManager.cpp
    popularityApi.h
    popularityApi.cpp
    uptimeTracker.h
    uptimeTracker.cpp
)

# ==================== LINKING ====================
if(USE_GUI_CHARTS)
    target_link_libraries(CheckOnlineBot PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
else()
    target_link_libraries(CheckOnlineBot PRIVATE Qt6::Core Qt6::Network)
endif()