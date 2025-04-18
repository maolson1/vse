cmake_minimum_required(VERSION 3.15)

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR "
FATAL: In-source builds are not allowed.
       You should create a separate directory for build files.
")
endif()

# Set policies for more modern cmake
cmake_policy(SET CMP0091 NEW) # Runtime flags
cmake_policy(SET CMP0092 NEW) # Warning flags

project(vse)

# Use static CRT
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(vse vse.c)
target_include_directories(vse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(vse PUBLIC /WX /W4 /sdl /permissive- /guard:cf /Zi)
target_compile_definitions(vse
  PUBLIC
    UNICODE _UNICODE
  PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
target_link_libraries(vse PUBLIC)
target_link_options(vse PUBLIC /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:FULL)
