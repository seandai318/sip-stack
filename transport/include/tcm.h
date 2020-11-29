/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file tcm.h
 ********************************************************/

#ifndef _TCM_H
#define _TCM_H


#include <netinet/in.h>

#include "osList.h"

#include "sipTransIntf.h"
//#include "sipTransport.h"
#include "sipTcm.h"
#include "diaTcm.h"


#if 0
typedef struct tpListenerInfo {
	int tcpFd;
	transportAppType_e appType;
} tpListenerInfo_t;
#endif


typedef struct tcpConnInfo {
    osListPlus_t appIdList;	//sipTrIdList;
} tcpConnInfo_t;


typedef struct tpQuarantine {
    bool isUsing;
    struct sockaddr_in peer;
    struct sockaddr_in local;
    uint64_t qTimerId;
} tpQuarantine_t;	//sipTpQuarantine_t;


typedef struct {
	osMBuf_t* pMsgBuf;
    uint64_t keepAliveTimerId;
    bool isPersistent;  //if isPersistent, keepAliveTimerId shall not be lauched, the connection shall be kept as long as possible
    bool isUsed;    //if this TCP connection is ever used during the keepalive period
	union {
		sipTpMsgState_t sipState;	
		diaTpMsgState_t diaState;
	};
} tpTcpMsgConnInfo_t;


typedef struct tpTcm {
    bool isUsing;       				//if this slot is current used
	transportAppType_e appType;
    bool isTcpConnDone; 				//if true, TCP connection has established
    int sockfd;         				//tcp fd
    struct sockaddr_in peer;
	struct sockaddr_in local;
	union {
    	tcpConnInfo_t tcpConn;          	//when sockfd < 0, contains the appIds who requested to initiate the tcp connection as a client.
    	tpTcpMsgConnInfo_t msgConnInfo;		//when sockfd >0, contains info about received message and the monitoring of active conn
	};
//		sipTcpInfo_t sipTcpInfo;		//tcpKeepAlive;//when isTcpConn = true, appType = TP_APP_TYPE_SIP
//		diaTcpInfo_t diaTcpInfo;		//when isTcpConn = true, appType = TP_APP_TYPE_DIAMETER
} tpTcm_t;


//only used when the same thread delivery, i.e., by tpAppMain.
typedef void (*notifyTcpConnUser_h)(osListPlus_t* pList, transportStatus_e connStatus, int tcpfd, struct sockaddr_in* pPeer);


void tcmInit(notifyTcpConnUser_h notifier[], int notifyNum);
tpTcm_t* tpGetTcm(struct sockaddr_in peer, transportAppType_e appType, bool isReseveOne);
tpTcm_t* tpGetTcmByFd(int tcpFd, struct sockaddr_in peer);
osStatus_e tpTcmAddFd(int tcpfd, struct sockaddr_in* peer, struct sockaddr_in* local, transportAppType_e appType);
tpTcm_t* tpGetConnectedTcm(int tcpFd);
osStatus_e tpDeleteTcm(int tcpfd, bool isNotifyApp);
void tpDeleteAllTcm();
osStatus_e tpTcmAddUser(tpTcm_t* pTcm, void* pTrId);
osStatus_e tpTcmUpdateConn(int tcpfd, bool isConnEstablished);
bool tpIsInQList(struct sockaddr_in peer);
osMBuf_t* tpTcmBufInit(tpTcm_t* pTcm, bool isAllocSipBuf);
//only usable when LM_TRANSPORT module is set to DEBUG level
void tpListUsedTcm();
void tpReleaseTcm(tpTcm_t* pTcm);
osStatus_e tpTcmCloseTcpConn(int tpEpFd, int tcpFd, bool isNotifyApp);

#endif
