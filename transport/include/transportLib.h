/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipConfig.c
 ********************************************************/


#ifndef _TRANSPORT_LIB_H
#define _TRANSPORT_LIB_H


#include <netinet/in.h>
#include <arpa/inet.h>

#include "osMBuf.h"
#include "osList.h"

#include "sipTcm.h"


typedef struct tpListenerInfo {
    transportAppType_e appType;
    int fd;
    struct sockaddr_in local;       //local address
} tpLocalAddrInfo_t;


//bool sipTpSafeGuideMsg(osMBuf_t* sipBuf, size_t len);
//ssize_t sipTpAnalyseMsg(osMBuf_t* pBuf, sipTpMsgState_t* pState, size_t chunkLen, size_t* pNextStart);
osStatus_e tpConvertPLton(transportIpPort_t* pIpPort, bool isIncPort, struct sockaddr_in* pSockAddr);
osStatus_e tpCreateTcp(int tpEpFd, struct sockaddr_in* peer, struct sockaddr_in* local, int* sockfd, int* connStatus);
transportAppType_e tpGetAppTypeFromLocalAddr(struct sockaddr_in* pLocal, bool isTcp, osList_t* pTcpAddrList, osList_t* pUdpAddrList);


#endif

