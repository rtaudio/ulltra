cmake_minimum_required(VERSION 3.5)
project(ulltra)


option(AUDIO_LINUX_PULSE "Build Linux PulseAudio API" ON)

set (CMAKE_CXX_STANDARD 11)


set (BUILD_TESTING OFF) # dont build rtaudio tests
add_definitions(-DMG_ENABLE_IPV6)


set(ULLTRA_LINKLIBS)

if(WIN32)
	#SET ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
	add_compile_options(/EHsc)
	list(APPEND ULLTRA_LINKLIBS winmm ws2_32)
	# ole32 
endif(WIN32)

if (UNIX AND NOT APPLE)
        #always build with ALSA
	find_package(ALSA)
	if (NOT ALSA_FOUND)
		message(FATAL_ERROR "ALSA API requested but no ALSA dev libraries found")
	endif()
	include_directories(${ALSA_INCLUDE_DIR})
	list(APPEND ULLTRA_LINKLIBS ${ALSA_LIBRARY})
	add_definitions(-D__LINUX_ALSA__)
	message(STATUS "Using Linux ALSA")



    if (AUDIO_LINUX_PULSE)
        find_library(PULSE_LIB pulse)
        find_library(PULSESIMPLE_LIB pulse-simple)
        list(APPEND ULLTRA_LINKLIBS ${PULSE_LIB} ${PULSESIMPLE_LIB})
        add_definitions(-D__LINUX_PULSE__)
        message(STATUS "Using Linux PulseAudio")
    endif (AUDIO_LINUX_PULSE)
	

    #debug
    add_definitions("-g -ggdb -D_DEBUG -D_GLIBCXX_DEBUG")

	set(SANITZE_FLAGS)
	
        if(ENABLE_ADDRESS_SANITIZER)
		set(SANITZE_FLAGS "${SANITZE_FLAGS} -fsanitize=leak -fsanitize=undefined -fsanitize=bounds")
		# address sanitizer (detects buffer xflows)
		set(SANITZE_FLAGS "${SANITZE_FLAGS} -O1 -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls")
		#clang:
		#add_definitions("-faddress-sanitizer -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls")		
		#enable memorysanitizer https://github.com/google/sanitizers/wiki/MemorySanitizer (detects uninitialized memory reads)
		#add_definitions("-fsanitize=memory -fPIE -pie")
        endif(ENABLE_ADDRESS_SANITIZER)

	SET( CMAKE_CXX_FLAGS  "${SANITZE_FLAGS} ${CMAKE_CXX_FLAGS}" )
	SET( CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${SANITZE_FLAGS}" )
endif (UNIX AND NOT APPLE)

add_subdirectory (rtt)
add_subdirectory (rtaudio)
add_subdirectory (fdk-aac)
add_subdirectory (autil)
add_subdirectory (masaj)


find_library(OPUS_LIB opus)
list(APPEND ULLTRA_LINKLIBS ${OPUS_LIB})


set(src_main ulltra.cpp Controller.cpp UlltraProto.cpp)

set(src_audio audio/AudioIOManager.cpp audio/AudioCoder.cpp audio/coders/AacCoder.cpp audio/WebAudio.cpp audio/coders/OpusCoder.cpp)
# autil/test.cpp
set(src_net net/Discovery.cpp net/LinkEval.cpp net/LLLink.cpp net/LLTcp.cpp net/LLUdpLink.cpp net/LLUdpQWave.cpp net/SimpleUdpReceiver.cpp net/LLCustomBlock.cpp)
set(src_streaming AudioStreamer.cpp AudioStreamerRx.cpp AudioStreamerTx.cpp OrderingBuffer.cpp)

add_executable(ulltra ${src_main} ${src_audio} ${src_net} ${src_streaming})

target_include_directories (ulltra PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${FDK_AAC_INCLUDES} fdk-aac/libSYS/include)

target_link_libraries(ulltra rtt rtaudio_static masaj fdk-aac autil ${ULLTRA_LINKLIBS})

source_group("main"            FILES ${src_main})
source_group("audio"           FILES ${src_audio})
source_group("net"             FILES ${src_net})
source_group("streaming"       FILES ${src_streaming})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ulltra)