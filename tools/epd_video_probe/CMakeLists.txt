# **Un projecte d'ESP-IDF de veritat, i petit** (0.190.127): the probe is one `main.cpp` and the framework's own components, o
# sigui que aquest fitxer no ha de saber res de lunokOS (docs/85).
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lunokos_epd_probe)
