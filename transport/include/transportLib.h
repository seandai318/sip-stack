#ifndef _TRANSPORT_LIB_H
#define _TRANSPORT_LIB_H


#include <netinet/in.h>
#include <arpa/inet.h>

#include "osMBuf.h"
#include "sipTcm.h"
//#include "sipTransport.h"


//bool sipTpSafeGuideMsg(osMBuf_t* sipBuf, size_t len);
//ssize_t sipTpAnalyseMsg(osMBuf_t* pBuf, sipTpMsgState_t* pState, size_t chunkLen, size_t* pNextStart);
osStatus_e tpConvertPLton(transportIpPort_t* pIpPort, bool isIncPort, struct sockaddr_in* pSockAddr);
osStatus_e tpCreateTcp(int tpEpFd, struct sockaddr_in* peer, struct sockaddr_in* local, int* sockfd, int* connStatus);


#endif

