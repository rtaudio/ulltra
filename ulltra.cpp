// ulltra.cpp : Definiert den Einstiegspunkt f�r die Konsolenanwendung.
//

#include "NetworkManager.h"

#ifndef WIN32
#include <unistd.h>
#endif

int main()
{
	NetworkManager netMgr;

	while (netMgr.isRunning()) {
#ifdef WIN32
		Sleep(1000);
#else
		usleep(1000*1000);
#endif
	}
    return 0;
}

