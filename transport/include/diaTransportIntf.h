/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file diaTransportIntf.h
 ********************************************************/

#ifndef _DIA_TRANSPORT_INTF_H
#define _DIA_TRANSPORT_INTF_H

#include <netinet/in.h>

#include "osMBuf.h"
#include "transportIntf.h"


typedef struct diaTransportMsgBuf {
    int tcpFd;      //tcpFd>=0
//    struct sockaddr_in peer;
//    void* diaId;    //id in diameter stack that started the connection request
    osMBuf_t* pDiaBuf;
} diaTransportMsgBuf_t;



typedef enum {
	DIA_TRANSPORT_MSG_TYPE_PEER_MSG,
	DIA_TRANSPORT_MSG_TYPE_TCP_CONN_STATUS,
} diaTransportMsgType_e;


typedef struct connStatusMsg {
	transportStatus_e connStatus;
	int fd;
//	struct sockaddr_in peer;
//	void* diaId;	//id in diameter stack that started the connection request 
} connStatusMsg_t; 


typedef struct diaTransportMsg {
	diaTransportMsgType_e tpMsgType;
    struct sockaddr_in peer;
	struct sockaddr_in local;
    void* diaId;    //id in diameter stack that started the connection request
	union {
		diaTransportMsgBuf_t peerMsg;
		connStatusMsg_t connStatusMsg;
	};
} diaTransportMsg_t;


#endif
