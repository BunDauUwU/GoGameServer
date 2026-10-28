cmake_minimum_required(VERSION 3.16)
project(GoGameServer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network WebSockets)

add_library(goGameServer STATIC
    Server.cpp Server.h ByoYomiClock.h
    src/Rules/GoMatch.cpp src/Rules/GoMatch.h
)
target_link_libraries(goGameServer PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
target_include_directories(goGameServer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Rules
)

qt_add_executable(appserver main.cpp)
target_link_libraries(appserver PRIVATE goGameServer)

include(GNUInstallDirs)
install(TARGETS appserver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
