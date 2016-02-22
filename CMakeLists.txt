cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

project (ulltra)

set (ulltra_VERSION_MAJOR 1)
set (ulltra_VERSION_MINOR 0)


include_directories(./ ./rtt "${PROJECT_BINARY_DIR}")

add_executable(ulltra ulltra.cpp)
add_library(rtt rtt/rtt.cpp)
add_library(ulltra-net Discovery.cpp NetworkManager.cpp)

# enable cxx11
target_compile_features(ulltra PRIVATE cxx_range_for)
target_compile_features(rtt PRIVATE cxx_range_for)
target_compile_features(ulltra-net PRIVATE cxx_range_for)


find_path (JACK_INC jack/jack.h)
find_library(JACK_LIB NAMES jack  )

find_library(PTHREAD_LIB NAMES pthread )

target_link_libraries (ulltra ulltra-net rtt ${JACK_LIB} ${PTHREAD_LIB})

#execute_process(COMMAND ./gcc-tune-flags
#    OUTPUT_VARIABLE GCC_TUNE_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
#add_definitions(${GCC_TUNE_FLAGS})

#debug
add_definitions("-g -ggdb")
