/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyConfig.h
 ********************************************************/

#ifndef _PROXY_CONFIG_H
#define _PROXY_CONFIG_H

#include "osTypes.h"
#include "transportIntf.h"


#define PROXY_CONFIG_HAS_REGISTRAR	1
#define PROXY_CONFIG_DEFINED_NEXT_HOP	false
#define PROXY_CONFIG_NEXT_HOP_IP	"10.10.10.10"
#define PROXY_CONFIG_NEXT_HOP_PORT	5060

#define SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM	20
#define SIP_TU_PROXY_MAX_EXTRA_HDR_DEL_NUM	20

bool proxyConfig_hasRegistrar();
bool proxyConfig_getNextHop(transportIpPort_t* pNextHop);


#endif
