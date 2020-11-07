/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportIntf.h
 ********************************************************/


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
	bool isUdpWaitResponse;	//whether the UDP needs to wait for response, if true, it will be the app's responsibility to close the udp fd
	bool isEphemeralPort;	//shall a EphemeralPort port be used or use a port provided by app
	int fd;					//for sending message, if fd != -1, the tp shall use the fd to send out the udp message, for receiving message, fd is filled with udp fd created if isUdpWaitResponse = true
} udpInfo_t;


typedef struct {
	bool isCom;		//=1, sends via com thread, otherwise, the same thread as the calling app, mainly for sip
    transportType_e tpType;
	union {
    	int tcpFd;              //only for TRANSPORT_TYPE_TCP.  If fd=-1, transport shall create one. for SIP response, the fd usually not 0, shall be the one what the request was received from
		udpInfo_t udpInfo;		//only for TRANSPORT_TYPE_UDP and TRANSPORT_TYPE_ANY
	};
	bool isPeerFqdn;			//if peer is in the form of ip:port or fqdn
	union {
		struct sockaddr_in peer;	//when isPeerFqdn = false
		osVPointerLen_t peerFqdn;	//when isPeerFqdn = true
	};
	struct sockaddr_in local;
	size_t protocolUpdatePos;		//viaProtocolPos for sip, if=0, do not update.  some protocols like SIP let the tp to determine tp protocol, and the protocol message has to be updated by tp accordingly.
} transportInfo_t;
	

typedef void (*tpLocalSendCallback_h)(transportStatus_e tStatus, int fd, osMBuf_t* pBuf);

void transport_localRegApp(transportAppType_e appType, tpLocalSendCallback_h callback);
transportStatus_e transport_localSend( transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd);
transportStatus_e transport_send(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd);
osStatus_e transport_closeTcpConn(int tcpFd, bool isCom);



#endif
