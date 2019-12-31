#include <string.h>

#include "sipConfig.h"


static char* hostIP = "10.11.12.13";
static int hostPort = 5060;


char* sipConfig_getHostIP()
{
	return hostIP;
}


int sipConfig_getHostPort()
{
	return hostPort;
}


void sipConfig_getHost(osPointerLen_t* host, int* port)
{
	host->p = hostIP;
	host->l = strlen(hostIP);
	*port = hostPort;
}

