#ifndef _TRANSPORT_CONFIG_H
#define _TRANSPORT_CONFIG_H

#include "osTypes.h"
#include "osPL.h"

#include "transportIntf.h"


#define DIA_CONFIG_TCP_BUFFER_SIZE	5000;
#define TRANSPORT_DNS_MAX_UDP_SIZE	512
#define TRANSPORT_MAX_TCP_LISTENER_NUM	5


transportType_e tpConfig_getTransport(transportAppType_e appType, osPointerLen_t* ip, int port, size_t msgSize);


#endif
