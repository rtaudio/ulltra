cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)

project (ulltra)

set (ulltra_VERSION_MAJOR 1)
set (ulltra_VERSION_MINOR 0)


include_directories(./ ./rtt "${PROJECT_BINARY_DIR}")

add_executable(ulltra ulltra.cpp)
add_library(rtt rtt/rtt.cpp)
add_library(mongoose mongoose/mongoose.c)
add_library(ulltra-cntrl Controller.cpp JsonHttpClient.cpp JsonHttpServer.cpp)
add_library(ulltra-net NetworkManager.cpp Discovery.cpp SimpleUdpReceiver.cpp LinkEval.cpp)
add_library(ulltra-lludp LLReceiver.cpp LLSender.cpp)
add_library(ulltra-proto UlltraProto.cpp)



# enable cxx11
target_compile_features(ulltra PRIVATE cxx_range_for)
target_compile_features(rtt PRIVATE cxx_range_for)
target_compile_features(ulltra-net PRIVATE cxx_range_for)
target_compile_features(ulltra-lludp PRIVATE cxx_range_for)
target_compile_features(ulltra-proto PRIVATE cxx_range_for)
target_compile_features(ulltra-cntrl PRIVATE cxx_range_for)


find_path (JACK_INC jack/jack.h)
find_library(JACK_LIB NAMES jack  )

find_library(PTHREAD_LIB NAMES pthread )

target_link_libraries (ulltra ulltra-net ulltra-cntrl ulltra-lludp ulltra-proto rtt mongoose ${JACK_LIB} ${PTHREAD_LIB})

#execute_process(COMMAND ./gcc-tune-flags
#    OUTPUT_VARIABLE GCC_TUNE_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
#add_definitions(${GCC_TUNE_FLAGS})

#debug
add_definitions("-g -ggdb -D_DEBUG")
