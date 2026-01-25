cmake_minimum_required(VERSION 3.10)

# Project Name and Language
project(Cryo C)

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
    
    # --- FIX: Point to the correct file ---
    list(APPEND SOURCE_FILES src/platform/mac_impl.c)
    
    # Find Frameworks (Including IOKit for Day 15)
    find_library(APP_SERVICES ApplicationServices)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(IO_KIT IOKit)
    
    # Create the Executable
    add_executable(Cryo ${SOURCE_FILES})
    
    # Link Mac Frameworks
    target_link_libraries(Cryo ${APP_SERVICES} ${CORE_FOUNDATION} ${IO_KIT})

elseif(WIN32)
    message(STATUS "Build System: Detected Windows (NT Kernel)")
    
    # Add Windows-specific implementation
    list(APPEND SOURCE_FILES src/platform/win_impl.c)
    
    # Create the Executable
    add_executable(Cryo ${SOURCE_FILES})
    
    # Link Windows Libraries
    target_link_libraries(Cryo kernel32 user32 psapi)

if (APPLE)
    add_custom_command(TARGET Cryo POST_BUILD
        COMMAND codesign --force --sign - "${CMAKE_BINARY_DIR}/Cryo"
        COMMENT "Signing Cryo to keep macOS permissions happy..."
    )
endif()

else()
    message(FATAL_ERROR "OS not supported. This project only runs on macOS and Windows.")
endif()