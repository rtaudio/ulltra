// ulltra.cpp : Definiert den Einstiegspunkt f�r die Konsolenanwendung.
//


#include "NetworkManager.h"

int main()
{
	NetworkManager netMgr;

	while (netMgr.isRunning()) {
		Sleep(1000);
	}
    return 0;
}

