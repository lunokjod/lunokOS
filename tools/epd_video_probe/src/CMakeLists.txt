# **El `main` del probe**: un fitxer, i cap llibreria del projecte gran (docs/85).
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ".")
