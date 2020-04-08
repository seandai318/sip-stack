#include <string.h>

#include "sipConfig.h"
#include "sipConfigPriv.h"


char* sipConfig_getHostIP()
{
	return SIP_CONFIG_LOCAL_IP;
}


int sipConfig_getHostPort()
{
	return SIP_CONFIG_LISTEN_PORT;
}


void sipConfig_getHost(osPointerLen_t* host, int* port)
{
	host->p = SIP_CONFIG_LOCAL_IP;
	host->l = strlen(SIP_CONFIG_LOCAL_IP);
	*port = SIP_CONFIG_LISTEN_PORT;
}


void sipConfig_getHostStr(char** ppHost, int* port)
{
	*ppHost = SIP_CONFIG_LOCAL_IP;
	*port = SIP_CONFIG_LISTEN_PORT;
}


sipTransport_e sipConfig_getTransport(osPointerLen_t* ip, int port)
{
	size_t n = sizeof(sipConfig_peerTpType) / sizeof(sipConfig_peerTpType[0]);
	for(int i=0; i<n; i++)
	{
		if(sipConfig_peerTpType[i].port == port && osPL_strcmp(ip, sipConfig_peerTpType[i].ip)== 0)
		{
			return sipConfig_peerTpType[i].transportType;
		}
	}

	return sipConfig_defaultTpType;
}	
