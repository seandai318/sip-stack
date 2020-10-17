/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportConfig.c
 ********************************************************/

#include "sipConfig.h"
#include "diaConfig.h"

#include "transportIntf.h"
#include "transportConfig.h"


transportType_e tpConfig_getTransport(transportAppType_e appType, osPointerLen_t* ip, int port, size_t msgSize)
{
	transportType_e tpType = TRANSPORT_TYPE_UDP;
	switch(appType)
	{
		case TRANSPORT_APP_TYPE_SIP:
			//to-do
			break;
		case TRANSPORT_APP_TYPE_DIAMETER:
			tpType = TRANSPORT_TYPE_TCP;
			break;
    	case TRANSPORT_APP_TYPE_DNS:
			if(msgSize > TRANSPORT_DNS_MAX_UDP_SIZE)
			{
				tpType = TRANSPORT_TYPE_TCP;
			}
			break;
    	case TRANSPORT_APP_TYPE_UNKNOWN:
		default:
			break;
	}

	return tpType;
}


int tpGetBufSize(transportAppType_e appType)
{
    switch(appType)
    {
        case TRANSPORT_APP_TYPE_SIP:
            return SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE;
            break;
        case TRANSPORT_APP_TYPE_DIAMETER:
			return DIA_CONFIG_TRANSPORT_TCP_BUFFER_SIZE;
            break;
        case TRANSPORT_APP_TYPE_DNS:
        case TRANSPORT_APP_TYPE_UNKNOWN:
        default:
            break;
    }

    return SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE;
}


