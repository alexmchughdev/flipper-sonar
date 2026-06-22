# Flipper Sonar ESP32-S2 bridge firmware.
# Build:  idf.py set-target esp32s2 && idf.py build
# Flash:  idf.py -p <PORT> flash monitor   (or ESP Web Tools, see manifest.json)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sonar_bridge)
