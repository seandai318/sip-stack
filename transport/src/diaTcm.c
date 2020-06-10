#include <endian.h>
#include <string.h>
#include "unistd.h"

#include "osResourceMgmt.h"

#include "transportCom.h"
#include "diaTransportIntf.h"
#include "diaTcm.h"
#include "tcm.h"


// pNextStart: if there is extra bytes, the position in the buf that the extra bytes starts
// return value: -1: expect more read() for the current dia packet, 0: exact dia packet, >1 extra bytes for next dia packet.
ssize_t diaTpAnalyseMsg(osMBuf_t* pDiaMsg, diaTpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart)
{
    if(!pDiaMsg || !pMsgState)
    {
        logError("null pointer, pDiaMsg=%p, pMsgState=%p.", pDiaMsg, pMsgState);
        return -1;
    }

logError("to-remove, pMsgState->msgLen=%d, pMsgState->receivedBytes=%d", pMsgState->msgLen, pMsgState->receivedBytes);
    int remaining = -1;
    *pNextStart = 0;
    if(pMsgState->msgLen == 0)
    {
        pMsgState->msgLen = htobe32(*(uint32_t*)pDiaMsg->buf) & 0xffffff;
        pMsgState->receivedBytes = 0;
    }

    if(chunkLen >= (pMsgState->msgLen - pMsgState->receivedBytes))
    {
        remaining = chunkLen - (pMsgState->msgLen - pMsgState->receivedBytes);
        *pNextStart = pMsgState->msgLen - pMsgState->receivedBytes;

        //reset state
        pMsgState->receivedBytes = 0;
        pMsgState->msgLen = 0;
        goto EXIT;
    }
    else
    {
        pMsgState->receivedBytes += chunkLen;
        goto EXIT;
    }

EXIT:
    return remaining;
}


osStatus_e tpProcessDiaMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len, bool* isForwardMsg)
{
    osStatus_e status = OS_STATUS_OK;

#if 0
    //mdebug(LM_TRANSPORT, "received TCP message, len=%d from fd(%d), bufLen=%ld, msg=\n%r", len, events[i].data.fd, bufLen, &dbgPL);
    //basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
    if(pTcm->msgConnInfo.pMsgBuf->pos == 0 && (pTcm->msgConnInfo.pMsgBuf->buf[0] < 'A' || pTcm->msgConnInfo.pMsgBuf->buf[0] > 'Z'))
    {
        mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->msgConnInfo.pMsgBuf->buf[0], len);
        if(sipTpSafeGuideMsg(pTcm->msgConnInfo.pMsgBuf, len))
        {
            goto EXIT;
        }
    }
#endif

	*isForwardMsg = false;
    size_t nextStart = 0;

    ssize_t remaining = diaTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.diaState, len, &nextStart);
	debug("remaining=%d", remaining);
        
	//remaining <0 when the pBuf only contins part of a message, that does not necessary the message is bigger than pBuf->size, rather the whole message has not been received, pBuf only partly filled
    if(remaining < 0)
    {
        mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
        goto EXIT;
    }
        
	osMBuf_t* pCurDiaBuf = pTcm->msgConnInfo.pMsgBuf;
    bool isBadMsg = pTcm->msgConnInfo.diaState.isBadMsg;
    //if isBadMsg, drop the current received dia message, and reuse the pDiaMsgBuf
    if(!tpTcmBufInit(pTcm, isBadMsg ? false : true))
    {
        logError("fails to init a TCM pDiaMsgBuf.");
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    //copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
    if(remaining > 0)
    {
        memcpy(pTcm->msgConnInfo.pMsgBuf->buf, &pCurDiaBuf->buf[nextStart], remaining);
        pTcm->msgConnInfo.pMsgBuf->end = remaining;
        pTcm->msgConnInfo.pMsgBuf->pos = 0;
    }

	//prepare the current buf to be forwarded
	pCurDiaBuf->pos = 0;
	pCurDiaBuf->end = nextStart;

    if(!isBadMsg)
    {
        pTcm->msgConnInfo.isUsed = true;

		*isForwardMsg = true;
        //tpServerForwardMsg(TRANSPORT_APP_TYPE_SIP, pCurSipBuf, tcpFd, &pTcm->peer);
    }
    else
    {
        mdebug(LM_TRANSPORT, "received a bad msg, drop.");
    }

EXIT:
    return status;
}


void diaTpNotifyTcpConnUser(osListPlus_t* pList, transportStatus_e connStatus, int tcpfd, struct sockaddr_in* pPeer)
{
    osIPCMsg_t ipcMsg;
	diaTransportMsg_t* pTpMsg = oszalloc(sizeof(diaTransportMsg_t), NULL);

	pTpMsg->tpMsgType = DIA_TRANSPORT_MSG_TYPE_TCP_CONN_STATUS;
	pTpMsg->connStatusMsg.connStatus = connStatus;
	pTpMsg->connStatusMsg.fd = tcpfd;
	if(connStatus == TRANSPORT_STATUS_TCP_SERVER_OK && pList)
	{
		logError("A diameter server gets TRANSPORT_STATUS_TCP_SERVER_OK, but pList for diaId is not NULL.")
		pList = NULL;
	}
	pTpMsg->diaId = pList ? pList->first : NULL;		//for TRANSPORT_STATUS_TCP_SERVER_OK, this shall be NULL
	if(pPeer)
	{
		pTpMsg->peer = *pPeer;
	}

    ipcMsg.interface = OS_DIA_TRANSPORT;
    ipcMsg.pMsg = (void*) pTpMsg;

    //to-do, need to go to hash table, to find the destination ipc id, for now, just forward to the first one.
    write(getLbFd(), (void*) &ipcMsg, sizeof(osIPCMsg_t));

	if(pList && pList->num > 1)
	{
	    osListElement_t* pLE = pList->more.head;
        while(pLE)
        {
            pTpMsg = oszalloc(sizeof(diaTransportMsg_t), NULL);

            pTpMsg->tpMsgType = DIA_TRANSPORT_MSG_TYPE_TCP_CONN_STATUS;
            pTpMsg->connStatusMsg.connStatus = connStatus;
    		pTpMsg->connStatusMsg.fd = tcpfd;
			if(pPeer)
			{
				pTpMsg->peer = *pPeer;
			}
            pTpMsg->diaId = pLE->data;

            ipcMsg.pMsg = (void*) pTpMsg;

            //to-do, need to go to hash table, to find the destination ipc id, for now, just forward to the first one.
            write(getLbFd(), (void*) &ipcMsg, sizeof(osIPCMsg_t));

            pLE = pLE->next;
        }
    }

    //clear appIdList
    osListPlus_clear(pList);
}

