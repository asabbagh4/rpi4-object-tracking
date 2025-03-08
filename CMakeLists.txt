cmake_minimum_required(VERSION 3.10)
project(my-video-app)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV
find_package(OpenCV REQUIRED)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "OpenCV not found!")
endif()

# Find GStreamer (Optional, for streaming)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0)
if (GST_FOUND)
  message(STATUS "GStreamer found")
else()
  message(STATUS "GStreamer NOT found - Streaming will be disabled.")
endif()

# Add the executable
add_executable(my-video-app src/main.cpp)

# Link libraries
target_link_libraries(my-video-app ${OpenCV_LIBS})
if(GST_FOUND)
    target_link_libraries(my-video-app ${GST_LIBRARIES})
endif()

# Installation (important for Buildroot later)
install(TARGETS my-video-app DESTINATION bin)