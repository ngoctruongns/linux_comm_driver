cmake_minimum_required(VERSION 3.16)
project(linux_comm_driver CXX)

# Usable either standalone (cmake -S . -B build && ctest) or via add_subdirectory()
# from a parent project such as a ROS 2 package.
set(IS_TOP_LEVEL FALSE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IS_TOP_LEVEL TRUE)
endif()

add_library(linux_comm_driver STATIC serial_linux.cpp)
target_include_directories(linux_comm_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(linux_comm_driver PUBLIC cxx_std_17)
set_property(TARGET linux_comm_driver PROPERTY POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
target_link_libraries(linux_comm_driver PUBLIC Threads::Threads)

if(IS_TOP_LEVEL)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(linux_comm_driver PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    include(CTest)  # defines BUILD_TESTING, default ON
endif()

# BUILD_TESTING is set by CTest above, and by colcon/ament in a ROS 2 workspace.
if(BUILD_TESTING)
    add_executable(test_serial_linux test/test_serial_linux.cpp)
    target_link_libraries(test_serial_linux PRIVATE linux_comm_driver)
    # openpty() lives in libutil; the tests drive the driver through a pty pair.
    target_link_libraries(test_serial_linux PRIVATE util)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_serial_linux PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME serial_linux COMMAND test_serial_linux)
    # The suite waits on real timeouts (~1 s total); this only catches a hang.
    set_tests_properties(serial_linux PROPERTIES TIMEOUT 60)
endif()
