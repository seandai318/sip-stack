/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyConfig.c
 ********************************************************/

#include <string.h>

#include "osTypes.h"

#include "proxyConfig.h"
#include "transportIntf.h"


bool proxyConfig_hasRegistrar()
{
	return PROXY_CONFIG_HAS_REGISTRAR;
} 


bool proxyConfig_getNextHop(transportIpPort_t* pNextHop)
{
	if(!PROXY_CONFIG_DEFINED_NEXT_HOP)
	{
		return false;
	}

	if(!pNextHop)
	{
		return false;
	}

	pNextHop->ip.p = PROXY_CONFIG_NEXT_HOP_IP;
	pNextHop->ip.l = strlen(PROXY_CONFIG_NEXT_HOP_IP);
	pNextHop->port = PROXY_CONFIG_NEXT_HOP_PORT;

	return true;
}	
