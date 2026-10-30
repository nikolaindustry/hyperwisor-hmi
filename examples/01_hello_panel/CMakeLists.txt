cmake_minimum_required(VERSION 3.16)

# The board support package and the touch drivers live in this repository.
set(EXTRA_COMPONENT_DIRS "../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_panel)
