#ifndef _SIP_TRANSPORT_SERVER_H
#define _SIP_TRANSPORT_SERVER_H

#include "sipConfig.h"

#include "sipTransportIntf.h"


typedef struct sipTransportServerSetting {
//    int epollId;    //if epollId = -1, thread has to create its own epollId
	int ownIpcFd[2];
    int timerfd;		//this is the write end of the timer module
    sipTransportIpPort_t local;
	int lbFd[SIP_CONFIG_TRANSACTION_THREAD_NUM];
} sipTransportServerSetting_t;



//void sipTransport_onTimeout(uint64_t timerId, void* ptr);
osStatus_e sipTransportServerInit(int pipefd[2], uint32_t bucketSize);
void* sipTransportServerStart(void* pData);
sipTransportStatus_e sipTpServer_send(sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf);


#endif
