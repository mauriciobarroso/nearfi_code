# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(SDKCONFIG "${CMAKE_BINARY_DIR/sdkconfig}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(NearFi)

if (${CONFIG_OTA_ENABLE})
	target_add_binary_data(${CMAKE_PROJECT_NAME}.elf ${CONFIG_OTA_SERVER_CERT} TEXT)
endif()