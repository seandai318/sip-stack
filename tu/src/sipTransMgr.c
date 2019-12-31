#include "osHash.h"
#include "osList.h"
#include "osTimer.h"

#include "sipHeader.h"
#include "sipTransaction.h"
#include "sipTransMgr.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipHdrVia.h"
#include "sipHdrMisc.h"
#include "sipTU.h"


extern osStatus_e sipTransNoInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransNoInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
extern osStatus_e sipTransInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);

typedef struct sipTrans_transIdCmp {
	bool isReq;
	sipTransId_t* pTransId;
} sipTrans_transIdCmp_t;

static osStatus_e sipTrans_onPeerMsg(osMBuf_t* sipBuf);
static osStatus_e sipTrans_onTUMsg(sipTransMsg2SM_t* pSipTUMsg);
static sipTransaction_t* sipTransHashLookup(osHash_t* sipTransHash, sipTransId_t* pTransId, osStatus_e* pStatus);
static void sipTransTimerFunc(uint64_t timerId, void* ptr);
static bool sipTrans_transIdCmp(osListElement_t *le, void *data);
static osStatus_e sipDecodeTransInfo(osMBuf_t* pSipMsg, sipTransInfo_t* pTransId);
static void sipTrans_delete(void* pData);
static void sipTransHashData_delete(void* pData);

static osHash_t* sipTransHash;


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
			status = sipTrans_onPeerMsg((osMBuf_t*)pData);
			goto EXIT;
			break;
        case SIP_TRANS_MSG_TYPE_TU:
			status = sipTrans_onTUMsg((sipTransMsg2SM_t*)pData);
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
			((sipTransaction_t*)pData)->smOnMsg(SIP_TRANS_MSG_TYPE_TX_FAILED, (sipTransaction_t*)pData, 0);
			break;
        default:
			logError("unexpected msgType (%d), ignore.", msgType);
			break;
	}
EXIT:
    return status;
}


osStatus_e sipTrans_onPeerMsg(osMBuf_t* sipBuf)
{
    osStatus_e status = OS_STATUS_OK;

	sipTransInfo_t sipTransInfo;
	status = sipDecodeTransInfo(sipBuf, &sipTransInfo);
	if(status != OS_STATUS_OK)
    {
        logError("fails to decode sipTransId.");
     	goto EXIT;
    }

	//check if is a ACK.  For ACK message, send to TU to find pTrans
	if(sipTransInfo.isRequest && sipTransInfo.transId.reqCode == SIP_METHOD_ACK)
	{
		sipTransMsg2SM_t ackMsg2SM={};
		ackMsg2SM.sipMsgType = SIP_TRANS_SIPMSG_ACK;
		ackMsg2SM.pSipMsg = sipBuf;
				
		sipTU_onMsg(SIP_TRANS_MSG_TYPE_PEER, &ackMsg2SM);
		goto EXIT;
	}

	sipTransaction_t* pTrans = sipTransHashLookup(sipTransHash, &sipTransInfo.transId, &status);
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
				osMBuf_dealloc(sipBuf);
				goto EXIT;
			}

			pTrans = osMem_zalloc(sizeof(sipTransaction_t), sipTrans_delete);
            pTrans->state = SIP_TRANS_STATE_NONE;
			pHashData->pData = pTrans;
			pTrans->pReq = sipBuf;
			pTrans->transId = sipTransInfo.transId;
			pTrans->pTransHashLE = osHash_add(sipTransHash, pHashData); 

			//forward to transaction state handler
			sipTransMsg2SM_t msg2SM;
			msg2SM.sipMsgType = SIP_TRANS_SIPMSG_REQUEST;
			msg2SM.pSipMsg = sipBuf;
			msg2SM.pTransInfo = &sipTransInfo;
			msg2SM.pTrans = pTrans;
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
			osMBuf_dealloc(sipBuf);
			goto EXIT;
		}
	}
	
	if(!sipTransInfo.isRequest)
	{
		osMBuf_dealloc(pTrans->pResp);
		pTrans->pResp = sipBuf;
	}

	//forward to transaction state handler
   	sipTransMsg2SM_t msg2SM;
    msg2SM.sipMsgType = sipTransInfo.isRequest ? SIP_TRANS_SIPMSG_REQUEST : SIP_TRANS_SIPMSG_RESPONSE;
    msg2SM.pSipMsg = sipBuf;
    msg2SM.pTrans = pTrans;
	msg2SM.pTransInfo = &sipTransInfo;
	pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_PEER, &msg2SM, 0);

EXIT:
	return status;
}

osStatus_e sipTrans_onTUMsg(sipTransMsg2SM_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

    sipTransaction_t* pTrans = pSipTUMsg->pTrans;
    if(pTrans == NULL)
    {
        if(pSipTUMsg->sipMsgType == SIP_TRANS_SIPMSG_REQUEST)
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
            pTrans->pReq = osMem_ref(pSipTUMsg->pSipMsg);
			pTrans->transId = pSipTUMsg->pTransInfo->transId;
            pTrans->pTransHashLE = osHash_add(sipTransHash, pHashData);

            //forward to transaction state handler
            pSipTUMsg->pTrans = pTrans;

			if(pSipTUMsg->pTransInfo->transId.reqCode == SIP_METHOD_INVITE)
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

	if(pSipTUMsg->sipMsgType == SIP_TRANS_SIPMSG_ACK)
	{
//to-do, shall we dealloc?        osMBuf_dealloc(pTrans->pACK);
		pTrans->pACK = pSipTUMsg->pSipMsg;

		pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_PEER, pSipTUMsg, 0);
		goto EXIT;
	}

	pTrans->smOnMsg(SIP_TRANS_MSG_TYPE_TU, pSipTUMsg, 0);

EXIT:
    return status;
}


sipTransaction_t* sipTransHashLookup(osHash_t* sipTransHash, sipTransId_t* pTransId, osStatus_e *status)
{
	*status = OS_STATUS_OK;
	sipTransaction_t* pTrans = NULL;

	if(!sipTransHash || !pTransId)
	{
		logError("null pointer, sipTransHash=%p, pTransId=%p.", sipTransHash, pTransId);
		*status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//now we have transactionId, we can search hash
	uint32_t key = osHash_getKeyPL(&pTransId->branchId, true);
	osListElement_t* pHashLE = osHash_lookup(sipTransHash, key, sipTrans_transIdCmp, pTransId);
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
	return osStartTimer(msec, sipTransTimerFunc, pData);
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
	if(pIdCmp->pTransId->reqCode == pTrans->transId.reqCode && osPL_cmp(&pIdCmp->pTransId->branchId, &pTrans->transId.branchId) == 0)
	{
		if(pIdCmp->isReq)
		{
			if(osPL_cmp(&pIdCmp->pTransId->host, &pTrans->transId.host) == 0)
			{
				if(pIdCmp->pTransId->port == pTrans->transId.port || (pIdCmp->pTransId->port == 0 && pTrans->transId.port == 5060) || (pIdCmp->pTransId->port == 5060 && pTrans->transId.port == 0))
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
static osStatus_e sipDecodeTransInfo(osMBuf_t* pSipMsg, sipTransInfo_t* pTransInfo)
{
    osStatus_e status = OS_STATUS_OK;
	size_t sipMsgHdrStartPos = 0;
    sipHdrVia_t* pDecodedViaHdr = NULL;
    sipHdrCSeq_t* pDecodedCSeqHdr = NULL;

    if(!pSipMsg || !pTransInfo)
    {
        logError("Null pointer, pSipMsg=%p, pTransInfo=%p.", pSipMsg, pTransInfo);
        status = OS_ERROR_NULL_POINTER;
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
	sipMsgHdrStartPos = pSipMsg->pos;

    if(firstLine.isReqLine)
    {
        pTransInfo->transId.reqCode = firstLine.u.sipReqLine.sipReqCode;
    }
	pTransInfo->isRequest = firstLine.isReqLine;
	pTransInfo->rspCode = pTransInfo->isRequest ? SIP_RESPONSE_INVALID : firstLine.u.sipStatusLine.sipStatusCode;

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
            osMBuf_t pSipMsgHdr;
            osMBuf_allocRef1(&pSipMsgHdr, pSipMsg, sipHdr.valuePos, sipHdr.value.l);

            pDecodedViaHdr = sipHdrParseByName(&pSipMsgHdr, SIP_HDR_VIA);
            if(pDecodedViaHdr == NULL)
            {
                logError("decode hdr (%d) error.", SIP_HDR_VIA);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            //extract top via branchId, host, port
            osListElement_t* pViaLE = pDecodedViaHdr->viaList.head;
            if(!pViaLE)
            {
                logError("null pointer, pViaLE");
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;                
            }

            sipHdrViaElement_t* pVia = pViaLE->data;
            if(!pVia)
            {
                logError("null pointer, pVia");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pTransInfo->transId.host = pVia->hostport.host;
            pTransInfo->transId.port = pVia->hostport.portValue;

            osList_t* pList = &pVia->viaParamList;
            osListElement_t* pLE = pList->head;
            while (pLE)
            {
                sipParamNameValue_t* pNameValue = pLE->data;
                if(osPL_strcmp(&pNameValue->name, "branch") == 0)
                {
                    pTransInfo->transId.branchId = pNameValue->value;
                    break;
                }

                pLE = pLE->next;
            }

            if(!pLE)
            {
                logError("via does not include branchId.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
        }

        if(sipHdr.nameCode == SIP_HDR_CSEQ && !isCSeqDecoded)
        {
            isCSeqDecoded = true;

            osMBuf_t pSipMsgHdr;
            osMBuf_allocRef1(&pSipMsgHdr, pSipMsg, sipHdr.valuePos, sipHdr.value.l);

            sipHdrCSeq_t* pDecodedCSeqHdr = sipHdrParseByName(&pSipMsgHdr, SIP_HDR_CSEQ);
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
    pSipMsg->pos = sipMsgHdrStartPos;
	if(status != OS_STATUS_OK)
	{
		pSipMsg->pos = 0;
	}
    osMem_deref(pDecodedViaHdr);
    osMem_deref(pDecodedCSeqHdr);

    return status;
}


static void sipTrans_delete(void* pData)
{
	if(!pData)
	{
		return;
	}

	sipTransaction_t* pTrans = pData;

	osMem_deref(pTrans->pReq);
	osMem_deref(pTrans->pResp);
	osMem_deref(pTrans->pACK);

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

