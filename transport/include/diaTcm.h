#ifndef _DIA_TCM_H
#define _DIA_TCM_H


#include "osMBuf.h"
#include "osList.h"

#include "transportIntf.h"



typedef struct diaTpMsgState {
	bool isBadMsg;				//this is not used now, a place holder
	uint32_t msgLen;			//dia msg size as indicated in the message
	uint32_t receivedBytes;		//the amount of bytes within a dia message that have been received.
} diaTpMsgState_t;


struct tpTcm;

// pNextStart: if there is extra bytes, the position in the buf that the extra bytes starts
// return value: -1: expect more read() for the current dia packet, 0: exact dia packet, >1 extra bytes for next dia packet.
ssize_t diaTpAnalyseMsg(osMBuf_t* pDiaMsg, diaTpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart);
osStatus_e tpProcessDiaMsg(struct tpTcm* pTcm, int tcpFd, ssize_t len, bool* isForwardMsg);
void diaTpNotifyTcpConnUser(osListPlus_t* pList, transportStatus_e connStatus, int tcpfd, struct sockaddr_in* pPeer);


#endif
