/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipAppMain.h
 ********************************************************/


#ifndef _SIP_APP_MAIN_H
#define _SIP_APP_MAIN_H

#include "sipTransportIntf.h"
#include "transportConfig.h"


typedef struct {
    transportAppType_e appType;
    transportIpPort_t local;
} tpClientLocalInfo_t;


typedef struct sipTransportClientSetting {
//    int epollId;    //if epollId = -1, thread has to create its own epollId
	int ownIpcFd[2];
    int timerfd;	//this is the write end of the timer module
    uint8_t tcpInfoNum;
    uint8_t udpInfoNum;
    tpClientLocalInfo_t clientTcpInfo[TRANSPORT_MAX_TCP_LISTENER_NUM];
    tpClientLocalInfo_t clientUdpInfo[TRANSPORT_MAX_UDP_LISTENER_NUM];    
    //transportIpPort_t local;
	sipTp_appStartup_h appStartup;	//hockup to start certain functions in application layer
	void* appStartupData;
} sipTransportClientSetting_t;


osStatus_e sipAppMainInit(int pipefd[2]);
void* sipAppMainStart(void* pData);
void tpLocal_appReg(transportAppType_e appType, tpLocalSendCallback_h callback);
sipTransportStatus_e sipTpClient_send(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf);
//tp will create one udp per appType.  when receiving this call, if the corresponding udp does not extist, tp will create one.
//tp also maintains a keep alive timer, if no access to this fd during the keep alive timer, the udp socket will be closed. 
transportStatus_e tpLocal_udpSend(transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* pFd);
int appMain_getTpFd();


#endif
