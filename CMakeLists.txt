cmake_minimum_required(VERSION 3.10)

# Project Name and Language
project(MacNap C)

# 1. Enforce C11 Standard (Modern C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 2. Add 'src' to the include path
include_directories(${CMAKE_SOURCE_DIR}/src)

# 3. Define the Main Source File
set(SOURCE_FILES src/main.c)

# 4. Platform Detection & Linking
if(APPLE)
    message(STATUS "Build System: Detected macOS (XNU Kernel)")
    
    # Add Mac-specific implementation
    list(APPEND SOURCE_FILES src/platform/mac_impl.c)
    
    # Find required Mac frameworks (for Window Management)
    find_library(APP_SERVICES ApplicationServices)
    find_library(CORE_FOUNDATION CoreFoundation)
    
    # Create the Executable
    add_executable(MacNap ${SOURCE_FILES})
    
    # Link Mac Frameworks
    target_link_libraries(MacNap ${APP_SERVICES} ${CORE_FOUNDATION})

elseif(WIN32)
    message(STATUS "Build System: Detected Windows (NT Kernel)")
    
    # Add Windows-specific implementation
    list(APPEND SOURCE_FILES src/platform/win_impl.c)
    
    # Create the Executable
    add_executable(MacNap ${SOURCE_FILES})
    
    # Link Windows Libraries
    # Kernel32: Standard OS calls
    # User32: Window management (GetForegroundWindow)
    # Psapi: Process Status API (Memory usage)
    target_link_libraries(MacNap kernel32 user32 psapi)

else()
    message(FATAL_ERROR "OS not supported. This project only runs on macOS and Windows.")
endif()