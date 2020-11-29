/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportConfig.h
 ********************************************************/


#ifndef _TRANSPORT_CONFIG_H
#define _TRANSPORT_CONFIG_H

#include "osTypes.h"
#include "osPL.h"

#include "transportIntf.h"


#define DIA_CONFIG_TCP_BUFFER_SIZE	5000;
#define TRANSPORT_DNS_MAX_UDP_SIZE	512
#define TRANSPORT_MAX_TCP_LISTENER_NUM	5
#define TRANSPORT_MAX_UDP_LISTENER_NUM  5

#define TRANSPORT_UDP_KEEP_ALIVE_TIME	60000


transportType_e tpConfig_getTransport(transportAppType_e appType, osPointerLen_t* ip, int port, size_t msgSize);
int tpGetBufSize(transportAppType_e appType);


#endif
