// ulltra.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#define _WINSOCKAPI_  // prevent winsock.h
#include <rtt/rtt.h>
#include "UlltraProto.h"
#include "Controller.h"


#ifndef _WIN32
#include <unistd.h>
#endif

#include <iostream>
#include<signal.h>


volatile int g_isRunning;
Controller *g_controller;

#ifdef _WIN32
void __CRTDECL sigintHandler(int sig)
#else
void sigintHandler(int sig)
#endif
{
	g_isRunning = false;
}

bool ulltraInit()
{
	LOG(logDEBUG) << "ulltraInit():";
	
	if (!RttThread::Init()) {
		LOG(logERROR) << "Failed to initialize rtt!";
		return false;
	}
	
	LOG(logDEBUG1) << "rtt initialized!";

	if (!UlltraProto::init()) {
		LOG(logERROR) << "Failed to initialize ulltra protocol!";
		return false;
	}
	
	LOG(logDEBUG1) << "UlltraProto initialized!";


	g_isRunning = true;
	signal(SIGINT, sigintHandler);

	LOG(logDEBUG1) << "creating controller...";
	Controller::Params params;
	params.nodesFileName = "nodes.txt";	
	g_controller = new Controller(params);

	LOG(logINFO) << "ulltra is running ..." << std::endl;
	return true;
}

#ifdef ANDROID
int ulltraMainAndroid()
{
	LOG(logINFO) << "Ulltra android init";
	return ulltraInit();
}

#endif


int main(int argc, const char* argv[])
{
	if (!ulltraInit())
		return 1;

	while (g_controller->isRunning() && g_isRunning) {
#ifdef _WIN32
		Sleep(1000);
#else
		usleep(1000*1000);
#endif
	}

	LOG(logINFO) << "exiting...";
    return 0;
}

// SIG33
#ifdef ANDROID
#ifdef __cplusplus
extern "C" {
#endif
void ulltraServiceStart()
{
	LOG(logINFO) << "Ulltra service started!";

	if(ulltraInit())
		LOG(logINFO) << "ulltra initialized, sleep looping...";

	while (g_controller->isRunning() && g_isRunning) {
		usleep(1000*1000);
	}
}
	
	void ulltraServiceStop()
	{
		g_isRunning = false;
	}
#ifdef __cplusplus
}
#endif
#endif
