cmake_minimum_required(VERSION 3.16)
project(SwClock VERSION 1.0.0 LANGUAGES C CXX)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Configure compiler flags for different build types
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Platform-specific settings
if(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__APPLE__")
endif()

# Source files
set(SWCLOCK_SOURCES
    src/sw_clock/swclock.c
    src/sw_clock/sw_adjtimex.c
    src/sw_clock/swclock_compat.c
)

set(SWCLOCK_HEADERS
    src/sw_clock/swclock.h
    src/sw_clock/sw_adjtimex.h
    src/sw_clock/swclock_compat.h
)

set(KF_SERVO_SOURCES
    src/kf_servo/kf_servo.c
)

set(KF_SERVO_HEADERS
    src/kf_servo/kf_servo.h
)

# Create the main SwClock library
add_library(swclock STATIC ${SWCLOCK_SOURCES} ${SWCLOCK_HEADERS})

# Create the KF Servo library  
add_library(kf_servo STATIC ${KF_SERVO_SOURCES} ${KF_SERVO_HEADERS})

# Create the AKF Servo library
file(GLOB AKF_SERVO_SOURCES "src/akf_servo/*.c")
file(GLOB AKF_SERVO_HEADERS "src/akf_servo/*.h")
add_library(akf_servo STATIC ${AKF_SERVO_SOURCES} ${AKF_SERVO_HEADERS})

# Create the AEKF Servo library
file(GLOB AEKF_SERVO_SOURCES "src/aekf_servo/*.c")
file(GLOB AEKF_SERVO_HEADERS "src/aekf_servo/*.h")
add_library(aekf_servo STATIC ${AEKF_SERVO_SOURCES} ${AEKF_SERVO_HEADERS})

# Create the EKF Servo library
file(GLOB EKF_SERVO_SOURCES "src/ekf_servo/*.c")
file(GLOB EKF_SERVO_HEADERS "src/ekf_servo/*.h")
add_library(ekf_servo STATIC ${EKF_SERVO_SOURCES} ${EKF_SERVO_HEADERS})

# Create the PI Servo library
file(GLOB PI_SERVO_SOURCES "src/pi_servo/*.c")
file(GLOB PI_SERVO_HEADERS "src/pi_servo/*.h")
add_library(pi_servo STATIC ${PI_SERVO_SOURCES} ${PI_SERVO_HEADERS})

# Create the Mix Servo library
file(GLOB MIX_SERVO_SOURCES "src/mix_servo/src/servo/*.c")
file(GLOB MIX_SERVO_HEADERS "src/mix_servo/src/servo/*.h")
add_library(mix_servo STATIC ${MIX_SERVO_SOURCES} ${MIX_SERVO_HEADERS})
target_include_directories(mix_servo PRIVATE src/akf_servo src/pi_servo)

# Include directories
target_include_directories(swclock PUBLIC src/sw_clock)
target_include_directories(kf_servo PUBLIC src/kf_servo)
target_include_directories(akf_servo PUBLIC src/akf_servo)
target_include_directories(aekf_servo PUBLIC src/aekf_servo)
target_include_directories(ekf_servo PUBLIC src/ekf_servo)
target_include_directories(pi_servo PUBLIC src/pi_servo)
target_include_directories(mix_servo PUBLIC src/mix_servo/src/servo)

# Link with required libraries
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(swclock ${MATH_LIBRARY})
    target_link_libraries(kf_servo ${MATH_LIBRARY})
    target_link_libraries(akf_servo ${MATH_LIBRARY})
    target_link_libraries(aekf_servo ${MATH_LIBRARY})
    target_link_libraries(ekf_servo ${MATH_LIBRARY})
    target_link_libraries(pi_servo ${MATH_LIBRARY})
    target_link_libraries(mix_servo ${MATH_LIBRARY})
endif()

# Platform-specific libraries
if(APPLE)
    # No additional libraries needed for macOS
elseif(UNIX)
    target_link_libraries(swclock pthread rt)
    target_link_libraries(kf_servo pthread)
    target_link_libraries(akf_servo pthread)
    target_link_libraries(aekf_servo pthread)
    target_link_libraries(ekf_servo pthread)
else()
    target_link_libraries(swclock pthread)
    target_link_libraries(kf_servo pthread)
    target_link_libraries(akf_servo pthread)
    target_link_libraries(aekf_servo pthread)
    target_link_libraries(ekf_servo pthread)
endif()

# Enable testing and find GTest
include(CTest)
enable_testing()

# Set the path for Homebrew on Apple Silicon and Intel Macs
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
endif()

find_package(GTest QUIET)

# Look for gtest source files
file(GLOB GTEST_SOURCES "gtests/*.cpp" "gtests/*.c")

if(GTest_FOUND AND GTEST_SOURCES)
    # Create gtests executable using modern CMake GTest targets
    add_executable(swclock_gtests ${GTEST_SOURCES})
    # Only link gtest_main which automatically includes gtest
    target_link_libraries(swclock_gtests PRIVATE swclock kf_servo akf_servo aekf_servo ekf_servo pi_servo mix_servo GTest::gtest_main)
    target_include_directories(swclock_gtests PRIVATE src/sw_clock src/kf_servo src/akf_servo)
    
    # Ensure logs directory exists for test output in project root
    add_custom_command(TARGET swclock_gtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/logs
        COMMENT "Creating logs directory for test output in project root"
    )
    
    # Add the test to CTest
    add_test(NAME SwClockGTests COMMAND swclock_gtests)
    message(STATUS "GoogleTest found - unit tests enabled")
elseif(GTEST_SOURCES)
    message(WARNING "GoogleTest not found. Install GoogleTest to build unit tests:")
    message(WARNING "  brew install googletest")
    message(STATUS "Run: ./setup.sh to install dependencies")
else()
    message(STATUS "No gtest source files found in gtests/ directory")
endif()

# Add sweep comparison executable
add_executable(sweep_compare_wifi src/sweep_compare_wifi.cpp)
target_link_libraries(sweep_compare_wifi PRIVATE swclock kf_servo akf_servo aekf_servo ekf_servo pi_servo mix_servo)
target_include_directories(sweep_compare_wifi PRIVATE src/sw_clock src/kf_servo src/akf_servo src/aekf_servo src/ekf_servo src/pi_servo src/mix_servo/src/servo)
set_target_properties(sweep_compare_wifi PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
target_link_libraries(sweep_compare_wifi PRIVATE Threads::Threads)

# Python tools integration
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_FOUND)
    # Create a custom target for Python analysis tools
    add_custom_target(python_tools ALL
        COMMENT "Python analysis tools available"
    )
    
    # Install Python tools
    install(PROGRAMS tools/plot_kf_wifi_perf.py
        DESTINATION bin
        RENAME plot_kf_wifi_perf
    )
    install(PROGRAMS tools/plot_akf_wifi_perf.py
        DESTINATION bin
        RENAME plot_akf_wifi_perf
    )
    install(PROGRAMS tools/plot_aekf_wifi_perf.py
        DESTINATION bin
        RENAME plot_aekf_wifi_perf
    )
    install(PROGRAMS tools/plot_ekf_wifi_perf.py
        DESTINATION bin
        RENAME plot_ekf_wifi_perf
    )
    
    # Add convenience targets for running Python tools
    add_custom_target(plot_perf
        COMMAND ${CMAKE_SOURCE_DIR}/tools/plot_latest_perf.sh ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/tools
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Run Kalman Filter performance analysis on latest test output"
        USES_TERMINAL
    )
    
    message(STATUS "Python tools integration enabled")
else()
    message(STATUS "Python3 not found - Python tools not integrated")
endif()

# Installation rules
install(TARGETS swclock kf_servo akf_servo aekf_servo ekf_servo
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${SWCLOCK_HEADERS} ${KF_SERVO_HEADERS} ${AKF_SERVO_HEADERS} ${AEKF_SERVO_HEADERS} ${EKF_SERVO_HEADERS}
    DESTINATION include
)

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C flags: ${CMAKE_C_FLAGS}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Release flags: ${CMAKE_C_FLAGS_RELEASE}")
endif()