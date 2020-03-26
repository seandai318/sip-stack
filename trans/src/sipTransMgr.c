#include <arpa/inet.h>

#include "osHash.h"
#include "osList.h"
#include "osTimer.h"
#include "osPreMemory.h"

#include "sipHeader.h"
#include "sipTransIntf.h"
#include "sipTransMgr.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipHdrVia.h"
#include "sipHdrMisc.h"
#include "sipTUIntf.h"
#include "sipTransportIntf.h"


extern osStatus_e sipTransNoInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransNoInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);

typedef struct sipTrans_transIdCmp {
	bool isReq;
	sipTransId_t* pTransId;
} sipTrans_transIdCmp_t;

static osStatus_e sipTrans_onPeerMsg(sipTransportMsgBuf_t* sipBuf);
static osStatus_e sipTrans_onTUMsg(sipTransMsg_t* pSipTUMsg);
static sipTransaction_t* sipTransHashLookup(osHash_t* sipTransHash, sipTransInfo_t* pTransInfo, uint32_t* pKey, osStatus_e* pStatus);
static void sipTransTimerFunc(uint64_t timerId, void* ptr);
static bool sipTrans_transIdCmp(osListElement_t *le, void *data);
static osStatus_e sipDecodeTransInfo(sipMsgBuf_t* pSipMsgBuf, sipTransInfo_t* pTransId);
static void sipTrans_delete(void* pData);
static void sipTransHashData_delete(void* pData);

//to-do, shall we add __thread?
static __thread osHash_t* sipTransHash;


osStatus_e sipTransInit(uint32_t bucketSize)
{
	osStatus_e status = OS_STATUS_OK;

	sipTransHash = osHash_create(bucketSize);
	if(!sipTransHash)
	{
		logError("fails to create sipTransHash.");
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		goto EXIT;
	}

EXIT:
	return status;
}


osStatus_e sipTrans_onMsg(sipTransMsgType_e msgType, void* pData, uint64_t timerId)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

	if(!pData)
	{
		logError("null pointer, pData.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	switch(msgType)
	{
		case SIP_TRANS_MSG_TYPE_PEER:
			status = sipTrans_onPeerMsg((sipTransportMsgBuf_t*)pData);
			goto EXIT;
			break;
        case SIP_TRANS_MSG_TYPE_TU:
			status = sipTrans_onTUMsg((sipTransMsg_t*)pData);
			goto EXIT;
        case SIP_TRANS_MSG_TYPE_TIMEOUT:
		{
			if(timerId == 0)
			{
				logError("timerId=0 for SIP_TRANS_MSG_TYPE_TIMEOUT.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
			sipTransaction_t* pTrans = pData;
			pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_TIMEOUT, (sipTransaction_t*)pData, timerId);
			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
        case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
		{
			sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pData)->pTransId;
logError("to-remove, TCM, pTrans=%p", pTrans);
			pTrans->smOnMsg(msgType, (sipTransportStatusMsg_t*)pData, 0);
			break;
		}
        default:
			logError("unexpected msgType (%d), ignore.", msgType);
			break;
	}

EXIT:
	DEBUG_END
    return status;
}


osStatus_e sipTrans_onPeerMsg(sipTransportMsgBuf_t* sipBuf)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

	sipTransInfo_t sipTransInfo;
	sipMsgBuf_t sipMsgBuf = {sipBuf->pSipBuf, 0, 0, 0};

	status = sipDecodeTransInfo(&sipMsgBuf, &sipTransInfo);
	if(status != OS_STATUS_OK)
    {
        logError("fails to decode sipTransId.");
     	goto EXIT;
    }

	//check if is a ACK.  For ACK message, send to TU to find pTrans
	if(sipTransInfo.isRequest && sipTransInfo.transId.reqCode == SIP_METHOD_ACK)
	{
		sipTUMsg_t ackMsg2TU={};
		ackMsg2TU.sipMsgType = SIP_TRANS_MSG_CONTENT_ACK;
		ackMsg2TU.pSipMsgBuf = &sipMsgBuf;
				
		sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &ackMsg2TU);
		goto EXIT;
	}

	uint32_t hashKey;
	sipTransaction_t* pTrans = sipTransHashLookup(sipTransHash, &sipTransInfo, &hashKey, &status);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipTransHashLookup.");
		goto EXIT;
	}
	if(pTrans == NULL)
	{ 
		if(sipTransInfo.isRequest)
		{
			//start a new transaction.
			osHashData_t* pHashData = osMem_zalloc(sizeof(osHashData_t), sipTransHashData_delete);
			if(!pHashData)
			{
				logError("fails to allocate pHashData.");
				status = OS_ERROR_MEMORY_ALLOC_FAILURE;
				osMem_deref(sipBuf);
				goto EXIT;
			}

			pTrans = osMem_zalloc(sizeof(sipTransaction_t), sipTrans_delete);
            pTrans->state = SIP_TRANS_STATE_NONE;
			pHashData->pData = pTrans;
			pTrans->req = sipMsgBuf;
			pTrans->transId = sipTransInfo.transId;
            pTrans->tpInfo.tcpFd = ((sipTransportMsgBuf_t*) sipBuf)->tcpFd;
			pTrans->tpInfo.tpType = pTrans->tpInfo.tcpFd == -1 ? SIP_TRANSPORT_TYPE_ANY : SIP_TRANSPORT_TYPE_TCP; 
			pTrans->tpInfo.isServer = ((sipTransportMsgBuf_t*) sipBuf)->isServer;
			char* ipaddr = osMem_zalloc(INET_ADDRSTRLEN, NULL);
			inet_ntop(AF_INET, &((sipTransportMsgBuf_t*) sipBuf)->peer.sin_addr, ipaddr, INET_ADDRSTRLEN);
			osPL_setStr(&pTrans->tpInfo.peer.ip, ipaddr, 0);
			pTrans->tpInfo.peer.port = ntohs(((sipTransportMsgBuf_t*) sipBuf)->peer.sin_port);
            debug("to-remove, pTrans=%p, received (sipTransportMsgBuf_t*) sipBuf=%p, tcpFd=%d, isServer=%d, pTrans->tpInfo.tcpFd=%d, pTrans->tpInfo.tpType=%d.", pTrans, sipBuf, ((sipTransportMsgBuf_t*) sipBuf)->tcpFd, ((sipTransportMsgBuf_t*) sipBuf)->isServer, pTrans->tpInfo.tcpFd, pTrans->tpInfo.tpType);
logError("to-remove, PEER, ip=%r, port=%d, ipaddr=%s", &pTrans->tpInfo.peer.ip, pTrans->tpInfo.peer.port, ipaddr);

            pHashData->hashKeyType = OSHASHKEY_INT;
            pHashData->hashKeyInt = hashKey;
            pHashData->pData = pTrans;
			pTrans->pTransHashLE = osHash_add(sipTransHash, pHashData); 

			//forward to transaction state handler
			sipTransMsg_t msg2SM;
			msg2SM.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
			msg2SM.request.sipTrMsgBuf.sipMsgBuf = sipMsgBuf;
			msg2SM.request.pTransInfo = &sipTransInfo;
			msg2SM.pTransId = pTrans;
			if(sipTransInfo.transId.reqCode == SIP_METHOD_INVITE)
			{
				pTrans->smOnMsg = sipTransInviteServer_onMsg;
			}
			else
			{
				pTrans->smOnMsg = sipTransNoInviteServer_onMsg;
			}

			pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_PEER, &msg2SM, 0); 
			
			goto EXIT;
		}
		else
		{
			logInfo("receive a unknown SIP response.");
			osMem_deref(sipBuf);
			goto EXIT;
		}
	}

	//forward to transaction state handler
   	sipTransMsg_t msg2SM;
    msg2SM.sipMsgType = sipTransInfo.isRequest ?  SIP_TRANS_MSG_CONTENT_REQUEST: SIP_TRANS_MSG_CONTENT_RESPONSE;
	if(msg2SM.sipMsgType == SIP_TRANS_MSG_CONTENT_REQUEST)
	{
    	msg2SM.request.sipTrMsgBuf.sipMsgBuf = sipMsgBuf;
	    msg2SM.request.pTransInfo = &sipTransInfo;
	}
	else
	{
		msg2SM.response.sipTrMsgBuf.sipMsgBuf = sipMsgBuf;
		msg2SM.response.rspCode = sipTransInfo.rspCode;
	}
    msg2SM.pTransId = pTrans;
	pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_PEER, &msg2SM, 0);

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTrans_onTUMsg(sipTransMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    sipTransaction_t* pTrans = pSipTUMsg->pTransId;
    if(pTrans == NULL)
    {
        if(pSipTUMsg->sipMsgType == SIP_TRANS_MSG_CONTENT_REQUEST)
        {
            //start a new transaction.
            osHashData_t* pHashData = osMem_zalloc(sizeof(osHashData_t), sipTransHashData_delete);
            if(!pHashData)
            {
                logError("fails to allocate pHashData.");
                status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                goto EXIT;
            }

            sipTransaction_t* pTrans = osMem_zalloc(sizeof(sipTransaction_t), sipTrans_delete);
            pHashData->pData = pTrans;
            pTrans->state = SIP_TRANS_STATE_NONE;
            pTrans->req.pSipMsg = osMem_ref(pSipTUMsg->request.sipTrMsgBuf.sipMsgBuf.pSipMsg);
			pTrans->transId = pSipTUMsg->request.pTransInfo->transId;

            pHashData->hashKeyType = OSHASHKEY_INT;
            pHashData->hashKeyInt = osHash_getKeyPL(&pSipTUMsg->request.pTransInfo->transId.viaId.branchId, true);
            pHashData->pData = pTrans;
            pTrans->pTransHashLE = osHash_add(sipTransHash, pHashData);

            //forward to transaction state handler
            pSipTUMsg->pTransId = pTrans;

			if(pSipTUMsg->request.pTransInfo->transId.reqCode == SIP_METHOD_INVITE)
			{
				pTrans->smOnMsg = sipTransInviteClient_onMsg;
            }
            else
            {
                pTrans->smOnMsg = sipTransNoInviteClient_onMsg;
            }
            pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_TU, pSipTUMsg, 0);

            goto EXIT;
        }
        else
        {
            logInfo("receive a unexpected sipMsgType from TU (%d), ignore.", pSipTUMsg->sipMsgType);
            goto EXIT;
        }
    }

	if(pSipTUMsg->sipMsgType == SIP_TRANS_MSG_CONTENT_ACK)
	{
//to-do, shall we dealloc?        osMBuf_dealloc(pTrans->pACK);
		pTrans->ack = pSipTUMsg->ack.sipTrMsgBuf.sipMsgBuf;

		pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_PEER, pSipTUMsg, 0);
		goto EXIT;
	}

	pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_TU, pSipTUMsg, 0);

EXIT:
	DEBUG_END
    return status;
}


sipTransaction_t* sipTransHashLookup(osHash_t* sipTransHash, sipTransInfo_t* pTransInfo, uint32_t* pKey, osStatus_e *status)
{
	*status = OS_STATUS_OK;
	sipTransaction_t* pTrans = NULL;

	if(!sipTransHash || !pTransInfo || !pKey)
	{
		logError("null pointer, sipTransHash=%p, pTransInfo=%p, pKey=%p.", sipTransHash, pTransInfo, pKey);
		*status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//now we have transactionInfo, we can search hash
	sipTrans_transIdCmp_t transIdCmp;
	transIdCmp.isReq = pTransInfo->isRequest;
	transIdCmp.pTransId = &pTransInfo->transId;
	*pKey = osHash_getKeyPL(&pTransInfo->transId.viaId.branchId, true);
	osListElement_t* pHashLE = osHash_lookup1(sipTransHash, *pKey, sipTrans_transIdCmp, &transIdCmp);
	if(!pHashLE)
	{
		goto EXIT;
	}

	pTrans = ((osHashData_t*)pHashLE->data)->pData;
	if(!pTrans)
	{
		logError("pTrans is NULL inside a pHashLE.");
		*status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

EXIT:
	return pTrans;
}


uint64_t sipTransStartTimer(time_t msec, void* pData)
{
	uint64_t timerId = osStartTimer(msec, sipTransTimerFunc, pData);
	return timerId;
}

static void sipTransTimerFunc(uint64_t timerId, void* ptr)
{
	sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TIMEOUT, ptr, timerId);
}	
 

bool sipTrans_transIdCmp(osListElement_t *le, void *data)
{
	bool isMatch = false;
	if(!le || !data)
	{
		goto EXIT;
	}

	osHashData_t* pHashData = le->data;
	sipTransaction_t* pTrans = pHashData->pData;
	if(!pTrans)
	{
        goto EXIT;
	}

	sipTrans_transIdCmp_t* pIdCmp = data;
	if(pIdCmp->pTransId->reqCode == pTrans->transId.reqCode && osPL_cmp(&pIdCmp->pTransId->viaId.branchId, &pTrans->transId.viaId.branchId) == 0)
	{
		if(pIdCmp->isReq)
		{
			if(osPL_cmp(&pIdCmp->pTransId->viaId.host, &pTrans->transId.viaId.host) == 0)
			{
				if(pIdCmp->pTransId->viaId.port == pTrans->transId.viaId.port || (pIdCmp->pTransId->viaId.port == 0 && pTrans->transId.viaId.port == 5060) || (pIdCmp->pTransId->viaId.port == 5060 && pTrans->transId.viaId.port == 0))
				{
					isMatch = true;
				}
			}
		}
		else
		{
			isMatch = true;
		}
	}

EXIT:
	return isMatch;
}


/* decode only necessary headers to extract transId */
static osStatus_e sipDecodeTransInfo(sipMsgBuf_t* pSipMsgBuf, sipTransInfo_t* pTransInfo)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	size_t sipMsgHdrStartPos = 0;
    sipHdrMultiVia_t* pDecodedViaHdr = NULL;
    sipHdrCSeq_t* pDecodedCSeqHdr = NULL;

    if(!pSipMsgBuf || !pTransInfo)
    {
        logError("Null pointer, pSipMsgBuf=%p, pTransInfo=%p.", pSipMsgBuf, pTransInfo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osMBuf_t* pSipMsg = pSipMsgBuf->pSipMsg;
	if(!pSipMsg)
	{
		logError("pSipMsgBuf->pSipMsg is null.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    pSipMsg->pos = 0;
    sipFirstline_t firstLine;
    status = sipParser_firstLine(pSipMsg, &firstLine);
    if(status != OS_STATUS_OK)
    {
        logError("could not parse the first line of sip message properly.");
        goto EXIT;
    }

	debug("sean-remove, isReq=%d", firstLine.isReqLine);
    if(firstLine.isReqLine)
    {
        pTransInfo->transId.reqCode = firstLine.u.sipReqLine.sipReqCode;
    }
	pTransInfo->isRequest = firstLine.isReqLine;
	pTransInfo->rspCode = pTransInfo->isRequest ? SIP_RESPONSE_INVALID : firstLine.u.sipStatusLine.sipStatusCode;
	pSipMsgBuf->rspCode = pTransInfo->rspCode;
	pSipMsgBuf->hdrStartPos = pSipMsg->pos; 

    sipRawHdr_t sipHdr;
    bool isCSeqDecoded = pTransInfo->isRequest ? true : false;
    bool isViaDecoded = false;

    bool isEOH = false;
    while (!isEOH)
    {
        if(sipDecodeOneHdrRaw(pSipMsg, &sipHdr, &isEOH) != OS_STATUS_OK)
        {
            logError("SIP HDR decode error.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

        if(sipHdr.nameCode == SIP_HDR_VIA && !isViaDecoded)
        {
            debug("nameCode=%d, isEOH=%d", sipHdr.nameCode, isEOH);
            isViaDecoded = true;

            //decode via
			pDecodedViaHdr = sipHdrParse(pSipMsg, SIP_HDR_VIA, sipHdr.valuePos, sipHdr.value.l);

#if 0
            osMBuf_t pSipMsgHdr;
            osMBuf_allocRef1(&pSipMsgHdr, pSipMsg, sipHdr.valuePos, sipHdr.value.l);

            pDecodedViaHdr = sipHdrParseByName(&pSipMsgHdr, SIP_HDR_VIA);
#endif
            if(pDecodedViaHdr == NULL)
            {
                logError("decode hdr (%d) error.", SIP_HDR_VIA);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			if(pDecodedViaHdr->pVia  == NULL)
			{
                logError("pDecodedViaHdr->u.pDecodedVia (%p) is NULL.", pDecodedViaHdr->pVia);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            //extract top via branchId, host, port
			pTransInfo->transId.viaId.branchId = pDecodedViaHdr->pVia->hdrValue.pBranch->value;
            pTransInfo->transId.viaId.host = pDecodedViaHdr->pVia->hdrValue.hostport.host;
            pTransInfo->transId.viaId.port = pDecodedViaHdr->pVia->hdrValue.hostport.portValue;
        }

        if(sipHdr.nameCode == SIP_HDR_CSEQ && !isCSeqDecoded)
        {
            isCSeqDecoded = true;

			sipHdrCSeq_t* pDecodedCSeqHdr = sipHdrParse(pSipMsg, SIP_HDR_CSEQ, sipHdr.valuePos, sipHdr.value.l);
#if 0
            osMBuf_t pSipMsgHdr;
            osMBuf_allocRef1(&pSipMsgHdr, pSipMsg, sipHdr.valuePos, sipHdr.value.l);

            sipHdrCSeq_t* pDecodedCSeqHdr = sipHdrParseByName(&pSipMsgHdr, SIP_HDR_CSEQ);
#endif
            if(pDecodedCSeqHdr == NULL)
            {
                logError("decode hdr (%d) error.", SIP_HDR_CSEQ);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pTransInfo->transId.reqCode = sipMsg_method2Code(&pDecodedCSeqHdr->method);
        }

        if(isCSeqDecoded && isViaDecoded)
        {
            break;
        }
    }

    if(isEOH)
    {
        logError("EOH, does not find Via header.")
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

EXIT:
	pSipMsg->pos = 0;

    osMem_deref(pDecodedViaHdr);
    osMem_deref(pDecodedCSeqHdr);

	if(status == OS_STATUS_OK)
	{
		pSipMsgBuf->isRequest = pTransInfo->isRequest;
		pSipMsgBuf->reqCode = pTransInfo->transId.reqCode;
	}

	DEBUG_END
    return status;
}


static void sipTrans_delete(void* pData)
{
	if(!pData)
	{
		return;
	}

	sipTransaction_t* pTrans = pData;

	osMBuf_dealloc(pTrans->req.pSipMsg);
	osMBuf_dealloc(pTrans->resp.pSipMsg);
	osMBuf_dealloc(pTrans->ack.pSipMsg);

	osDPL_dealloc((osDPointerLen_t*)&pTrans->tpInfo.peer.ip);
	osDPL_dealloc((osDPointerLen_t*)&pTrans->tpInfo.local.ip);
	//to-do, shall we also stop the timers?
	//no need to free pTransHashLE as it is freed as part of delete a hash entry.  as a matter of fact, sipTrans is freed inside pTransHashLE free
}


static void sipTransHashData_delete(void* pData)
{
    if(!pData)
    {
        return;
    }

	osHashData_t* pHashData = pData;
	osMem_deref(pHashData->pData);
}

