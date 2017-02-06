cmake_minimum_required(VERSION 3.6)
project(ulltra)

set (CMAKE_CXX_STANDARD 11)
set (BUILD_TESTING OFF)

set(ULLTRA_LINKLIBS)

if(WIN32)
	#SET ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
	add_compile_options(/EHsc)
	list(APPEND ULLTRA_LINKLIBS winmm ws2_32)
	# ole32 
endif(WIN32)

add_subdirectory (rtt)
add_subdirectory (pclog)
add_subdirectory (rtaudio)
add_subdirectory (fdk-aac)



set(src_main ulltra.cpp Controller.cpp UlltraProto.cpp)
set(src_audio audio/AudioIOManager.cpp audio/AudioCoder.cpp audio/coders/AacCoder.cpp audio/WebAudio.cpp autil/test.cpp)
set(src_net net/Discovery.cpp net/JsonHttpClient.cpp net/JsonHttpServer.cpp net/JsonNode.cpp net/LinkEval.cpp net/LLLink.cpp net/LLTcp.cpp net/LLUdpLink.cpp net/LLUdpQWave.cpp net/SimpleUdpReceiver.cpp net/LLCustomBlock.cpp)
set(src_streaming AudioStreamer.cpp AudioStreamerRx.cpp AudioStreamerTx.cpp OrderingBuffer.cpp)

add_library(mongoose mongoose/mongoose.c frozen/frozen.c)


add_executable(ulltra ${src_main} ${src_audio} ${src_mongoose} ${src_net} ${src_streaming})

target_include_directories (ulltra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${FDK_AAC_INCLUDES} fdk-aac/libSYS/include)

target_link_libraries(ulltra pclog rtt rtaudio_static mongoose fdk-aac ${ULLTRA_LINKLIBS})

source_group("main"            FILES ${src_main})
source_group("audio"           FILES ${src_audio})
source_group("mongoose"        FILES ${src_mongoose})
source_group("net"             FILES ${src_net})
source_group("streaming"       FILES ${src_streaming})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ulltra)