# The following four lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS $ENV{SQUIDWARD_PATH}/components/sq_wifi $ENV{SQUIDWARD_PATH}/components/sq_uart)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(mqtts)

target_add_binary_data(mqtts.elf "main/mqtt_server_ca.pem" TEXT)
