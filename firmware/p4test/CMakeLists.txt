# Bring-up test for ESP32-P4 panel candidates: chip and memory report, an I2C
# (Inter-Integrated Circuit) scan, the display over MIPI DSI (Mobile Industry
# Processor Interface, Display Serial Interface) with a test pattern, and the
# touch points.  Nothing of the rcbench panel runs here.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rcbench-p4test)
