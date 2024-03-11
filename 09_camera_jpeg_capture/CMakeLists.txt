cmake_minimum_required(VERSION 3.0)
project(camera_jpeg_capture)


set(ARGUS_UTILS_DIR "/usr/src/jetson_multimedia_api/argus/samples/utils")
set(ARGUS_INCLUDE_DIR "/usr/src/jetson_multimedia_api/argus/include/Argus")
set(ARGUS_LIBRARIES "/home/ctnano/Desktop/jetson_multimedia_api/argus/build/samples/utils/")

# Use a wildcard to find all .cpp files in the directory
file(GLOB APP_SOURCES ${ARGUS_UTILS_DIR}/*.cpp)

# Define the include directories
set(APP_INCLUDE_DIRS
    /usr/src/jetson_multimedia_api/argus/include
    /usr/src/jetson_multimedia_api/include
    /usr/local/cuda/include
    ${ARGUS_UTILS_DIR}
)

# Add executable
add_executable(camera_jpeg_capture ${APP_SOURCES})

# Set C++ standard
set_property(TARGET camera_jpeg_capture PROPERTY CXX_STANDARD 11)

# Add include directories
target_include_directories(camera_jpeg_capture PRIVATE ${APP_INCLUDE_DIRS})

# Add additional linker flags
set(APP_LINK_LIBRARIES
    /usr/lib/aarch64-linux-gnu/tegra/libnveglstream_camconsumer.so
    /usr/lib/aarch64-linux-gnu/tegra/libnvargus_socketclient.so
    /usr/lib/aarch64-linux-gnu/tegra/libnvbuf_utils.so.1.0.0
    /usr/lib/aarch64-linux-gnu/libpthread.so
    # /usr/lib/aarch64-linux-gnu/libEGL.so  # Add EGL library
    # /usr/lib/aarch64-linux-gnu/libGLESv2.so  # Add GLESv2 library
    # /usr/lib/aarch64-linux-gnu/tegra/libcuda.so
    
)
target_link_libraries(camera_jpeg_capture PRIVATE ${APP_LINK_LIBRARIES})