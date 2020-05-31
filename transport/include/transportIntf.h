#ifndef _TRANSPORT_INTF_H
#define _TRANSPORT_INTF_H


#include <netinet/in.h>

#include "osTypes.h"
#include "osPL.h"
#include "osMBuf.h"


typedef enum {
	TRANSPORT_APP_TYPE_SIP,
	TRANSPORT_APP_TYPE_DIAMETER,
	TRANSPORT_APP_TYPE_DNS,
	TRANSPORT_APP_TYPE_UNKNOWN,
	TRANSPORT_APP_TYPE_COUNT,
} transportAppType_e;


typedef enum {
	TRANSPORT_STATUS_UDP,
	TRANSPORT_STATUS_TCP_SERVER_OK,	//TCP server received a conn establishment req and conn is established
	TRANSPORT_STATUS_TCP_OK,	//TCP client conn is established
	TRANSPORT_STATUS_TCP_CONN,	//TCP CONN establish is ongoing
	TRANSPORT_STATUS_TCP_FAIL,
	TRANSPORT_STATUS_FAIL,
} transportStatus_e;


//this needs to be the same as sipTransport_e @sipUriParam.h, to-do: remove sipTransport_e
typedef enum {
    TRANSPORT_TYPE_UDP,
    TRANSPORT_TYPE_TCP,
    TRANSPORT_TYPE_SCTP,	//not implemented now
    TRANSPORT_TYPE_TLS,		//not implemented now
    TRANSPORT_TYPE_ANY,     //unknown transport type
} transportType_e;


//to-do, this definition to be removed after reconcile both data type. sipTransportStatus_e is to be removed
typedef transportStatus_e sipTransportStatus_e;


typedef struct {
    osPointerLen_t ip;
    int port;
} transportIpPort_t;


typedef struct {
	 bool isUdpWaitResponse;
	 bool isEphemeralPort;
} udpInfo_t;


typedef struct {
	bool isCom;		//=1, sends via com thread, otherwise, the same thread as the calling app, mainly for sip
    transportType_e tpType;
	union {
    	int tcpFd;              //only for TRANSPORT_TYPE_TCP.  If fd=-1, transport shall create one. for SIP response, the fd usually not 0, shall be the one what the request was received from
		udpInfo_t udpInfo;		//only for TRANSPORT_TYPE_UDP and TRANSPORT_TYPE_ANY
	};
	struct sockaddr_in peer;
	struct sockaddr_in local;
#if 0
	bool isSockAddr;
	union {
    	transportIpPort_t peer;			//if isSockAddr = 0;
		struct sockaddr_in peer_in;		//if isSockAddr = 1;
	};
	uinon {
    	transportIpPort_t local;		//if isSockAddr = 0;
		struct sockaddr_in local_in;	//if isSockAddr = 1;
	};
#endif
	size_t protocolUpdatePos;		//viaProtocolPos for sip, if=0, do not update.  some protocols like SIP let the tp to determine tp protocol, and the protocol message has to be updated by tp accordingly.
} transportInfo_t;
	

transportStatus_e transport_send(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd);
osStatus_e transport_closeTcpConn(int tcpFd, bool isCom);



#endif
