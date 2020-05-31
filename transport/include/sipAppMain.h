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
sipTransportStatus_e sipTpClient_send(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf);


#endif
