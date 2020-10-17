/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportCom.h
 ********************************************************/

#ifndef _TRANSPORT_COM_H
#define _TRANSPORT_COM_H

#include "sipConfig.h"

//#include "sipTransportIntf.h"
#include "transportIntf.h"
#include "transportConfig.h"



typedef struct {
	transportAppType_e appType;
	transportIpPort_t local;
} tpServerTcpInfo_t;


typedef struct sipTransportServerSetting {
//    int epollId;    //if epollId = -1, thread has to create its own epollId
	int ownIpcFd[2];
    int timerfd;		//this is the write end of the timer module
	uint8_t tcpInfoNum;
	tpServerTcpInfo_t serverTcpInfo[TRANSPORT_MAX_TCP_LISTENER_NUM];
	transportIpPort_t udpLocal;	//for now, only one for SIP
	int lbFd[SIP_CONFIG_TRANSACTION_THREAD_NUM];
} transportMainSetting_t;



//void sipTransport_onTimeout(uint64_t timerId, void* ptr);
osStatus_e transportMainInit(int pipefd[2], uint32_t bucketSize);
void* transportMainStart(void* pData);
//sipTransportStatus_e tpServer_send(transportInfo_t* pTpInfo, osMBuf_t* pSipBuf);
osStatus_e tpStartTcpConn(transportIpPort_t* local, transportIpPort_t* peer, bool isAnyLocalPort);
//send TCP/UDP message, create TCP/UDP if necessary
transportStatus_e com_send(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd);
//osStatus_e com_closeTcpConn(int tcpFd);
int getLbFd();
int com_getTpFd();


#endif
