# Lua 5.4 (MIT licence, https://www.lua.org) built as a static C library.
# Add to your top-level CMakeLists.txt:
#   add_subdirectory(third_party/lua)
#   target_link_libraries(<your-target> PRIVATE lua54)
cmake_minimum_required(VERSION 3.16)
project(lua54 C)

file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(lua54 STATIC ${LUA_SOURCES})
target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lua54 PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(UNIX)
    target_compile_definitions(lua54 PRIVATE LUA_USE_POSIX)
endif()
