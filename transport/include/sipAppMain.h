/* Copyright 2020, 2019, Sean Dai
 */

#ifndef _SIP_APP_MAIN_H
#define _SIP_APP_MAIN_H

#include "sipTransportIntf.h"



typedef struct sipTransportClientSetting {
//    int epollId;    //if epollId = -1, thread has to create its own epollId
	int ownIpcFd[2];
    int timerfd;	//this is the write end of the timer module
    transportIpPort_t local;
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
