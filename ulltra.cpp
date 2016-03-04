// ulltra.cpp : Definiert den Einstiegspunkt f�r die Konsolenanwendung.
//

#define _WINSOCKAPI_  // prevent winsock.h
#include <rtt.h>
#include"UlltraProto.h"
#include "Controller.h"


#ifndef _WIN32
#include <unistd.h>
#endif

#include <iostream>
#include<signal.h>

#include "WindowsMM.h"

bool g_isRunning;

#ifdef _WIN32
void __CRTDECL sigintHandler(int sig)
#else
void sigintHandler(int sig)
#endif
{
	g_isRunning = false;
}

int main(int argc, const char* argv[])
{
	RttThread::Init();

	if (!UlltraProto::init()) {
		std::cerr << "Failed to initialize ulltra protocol!" << std::endl;
		return 1;
	}

	auto wmm = new WindowsMM();
	wmm->init();
	LOG(logINFO) << "Available streams: " << wmm->getStreams();
		

	g_isRunning = true;
	signal(SIGINT, sigintHandler);

    Controller::Params params;
    params.nodesFileName = "nodes.txt";

    Controller controller(params);

	LOG(logINFO) << "ulltra is running ..." << std::endl;

	while (controller.isRunning() && g_isRunning) {
#ifdef _WIN32
		Sleep(1000);
#else
		usleep(1000*1000);
#endif
	}

	LOG(logINFO) << "exiting..." << std::endl;
    return 0;
}

