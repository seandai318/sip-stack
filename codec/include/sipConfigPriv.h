//this file shall only be included in sipConfig.h


#ifndef _SIP_CONFIG_PRIV_H
#define _SIP_CONFIG_PRIV_H


#include "sipUriparam.h"


typedef struct sipConfigTpType {
    char* ip;
    int port;
    sipTransport_e transportType;
} sipConfigTpType_t;



static sipTransport_e sipConfig_defaultTpType = SIP_TRANSPORT_TYPE_ANY;


static sipConfigTpType_t sipConfig_peerTpType[] ={	\
	{"10.12.13.14", 5060, SIP_TRANSPORT_TYPE_TCP}, 
	{"10.12.13.15", 5060, SIP_TRANSPORT_TYPE_UDP}}; 
	


#endif
 
