cmake_minimum_required(VERSION 3.10)
project(my-video-app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "OpenCV not found!")
endif()

# Find GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-base-1.0) # Add IMPORTED_TARGET

if (GST_FOUND)
  message(STATUS "GStreamer found")
  add_definitions(-DGST_FOUND) # Define the preprocessor macro
else()
  message(STATUS "GStreamer NOT found - Streaming will be disabled.")
endif()

# Add the executable
add_executable(my-video-app src/main.cpp)

# Link libraries
target_link_libraries(my-video-app ${OpenCV_LIBS})
if(GST_FOUND)
    target_link_libraries(my-video-app PkgConfig::GST) # Use the imported target
endif()

# Installation (important for Buildroot later)
install(TARGETS my-video-app DESTINATION bin)