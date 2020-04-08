#ifndef _PROXY_CONFIG_H
#define _PROXY_CONFIG_H

#include "osTypes.h"
#include "sipTransport.h"


#define PROXY_CONFIG_HAS_REGISTRAR	1
#define PROXY_CONFIG_DEFINED_NEXT_HOP	false
#define PROXY_CONFIG_NEXT_HOP_IP	"10.10.10.10"
#define PROXY_CONFIG_NEXT_HOP_PORT	5060



bool proxyConfig_hasRegistrar();
bool proxyConfig_getNextHop(sipTransportIpPort_t* pNextHop);


#endif
