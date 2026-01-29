cmake_minimum_required(VERSION 3.20)
project(WiimoteBridge)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/dolphin_src)

# Source files for the main application
set(SOURCES
    src/main.cpp
    src/system_tray.cpp
    src/wiimote_manager.cpp
    src/registry_utils.cpp
)

set(HEADERS
    include/system_tray.h
    include/wiimote_manager.h
    include/registry_utils.h
    include/resource.h
)

# Copy Dolphin pairing logic files
set(DOLPHIN_SOURCES
    dolphin_src/wiimote_pairing.cpp
)

set(DOLPHIN_HEADERS
    dolphin_src/wiimote_pairing.h
)

# Resource file for embedding the icon
set(RESOURCE_FILES
    resources.rc
)

# Create the executable
add_executable(WiimoteBridge WIN32 
    ${SOURCES} 
    ${HEADERS}
    ${DOLPHIN_SOURCES}
    ${DOLPHIN_HEADERS}
    ${RESOURCE_FILES}
)

# Link libraries
target_link_libraries(WiimoteBridge 
    PRIVATE
    User32.lib
    Shell32.lib
    Advapi32.lib
    BluetoothAPIs.lib
    Bthprops.lib
    SetupAPI.lib
    Cfgmgr32.lib
    Hid.lib
    ws2_32.lib
)

# Set output directory
set_target_properties(WiimoteBridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy icon file to output directory
add_custom_command(TARGET WiimoteBridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/wiimoteicon.ico"
        "$<TARGET_FILE_DIR:WiimoteBridge>/wiimoteicon.ico"
    COMMENT "Copying icon file..."
)

# Enable Windows subsystem
if(MSVC)
    target_link_options(WiimoteBridge PRIVATE /SUBSYSTEM:WINDOWS)
endif()
