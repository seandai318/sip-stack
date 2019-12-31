#include "osMisc.h"
#include "osTimer.h"
#include "osHash.h"

#include "sipTransaction.h"
#include "sipMsgRequest.h"
#include "sipTransaction.h"
#include "sipTransMgr.h"
#include "sipTU.h"
#include "sipTransport.h"
#include "sipConfig.h"


static osStatus_e sipTransISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateConfirmed_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osMBuf_t* sipTransIS_build100Trying(osMBuf_t* pSipBufReq);


osStatus_e sipTransInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipTransaction_t* pTrans = pMsg;
    if(msgType == SIP_TRANS_MSG_TYPE_PEER || msgType == SIP_TRANS_MSG_TYPE_TU)
    {
        pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;
    }

    if(!pTrans)
    {
        logError("null pointer, pTrans, for msgType (%d).", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    switch(pTrans->state)
	{
        case SIP_TRANS_STATE_NONE:
            status = sipTransISStateNone_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_PROCEEDING:
            status = sipTransISStateProceeding_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_CONFIRMED:
            status = sipTransISStateConfirmed_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_COMPLETED:
            status = sipTransISStateCompleted_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_TERMINATED:
        default:
            logError("received a timeout event that has invalid transaction state (%d),", (sipTransaction_t*)pTrans->state);
            status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
    return status;
}


osStatus_e sipTransISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(msgType != SIP_TRANS_MSG_TYPE_PEER)
    {
        logError("receive unexpected msgType (%d) in ISStateNone.", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    sipTransaction_t* pTrans = ((sipTransMsg2SM_t*) pMsg)->pTrans;
    if(!pTrans)
    {
        logError("null pointer, pTrans, for msgType (%d).", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(((sipTransMsg2SM_t*) pMsg)->sipMsgType != SIP_TRANS_SIPMSG_REQUEST)
	{
		logError("received a unexpected response.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

//    sipTransport_send(pTrans->pReq, pTrans);

    sipTransISEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
	
EXIT:
	return status;
}


osStatus_e sipTransISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	switch(msgType)
	{
		case SIP_TRANS_MSG_TYPE_PEER:
		{
			sipTransaction_t* pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;
			sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
			break;
		}
		case SIP_TRANS_MSG_TYPE_TU:
		{
			if(((sipTransMsg2SM_t*)pMsg)->sipMsgType != SIP_TRANS_SIPMSG_RESPONSE)
			{
				logError("received unexpected sipMsgType (%d), ignore.", ((sipTransMsg2SM_t*)pMsg)->sipMsgType);
				goto EXIT;
			}

            sipTransaction_t* pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;
            sipResponse_e rspCode = ((sipTransMsg2SM_t*)pMsg)->pTransInfo->rspCode;
			osMem_deref(pTrans->pResp);
			pTrans->pResp = osMem_ref(((sipTransMsg2SM_t*)pMsg)->pSipMsg);
            if(rspCode >= 100 && rspCode < 200)
            {
                sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
            }
            else if(rspCode >= 200 && rspCode < 300)
            {
				sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
			else
			{
                sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
                sipTransISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			sipTransaction_t* pTrans = pMsg;

			sipTU_onMsg(msgType, pTrans);
			sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			break;
		}
        default:
            logError("received unexpected msgType (%d), ignore.", msgType);
            break;
    }

EXIT:
	return status;
}


osStatus_e sipTransISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    switch(msgType)
    {
        case SIP_TRANS_MSG_TYPE_PEER:
		{
            sipTransaction_t* pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;
			if(((sipTransMsg2SM_t*)pMsg)->sipMsgType == SIP_TRANS_SIPMSG_REQUEST)
			{
				sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
			}
			else if(((sipTransMsg2SM_t*)pMsg)->sipMsgType == SIP_TRANS_SIPMSG_ACK)
			{
				sipTransISEnterState(SIP_TRANS_STATE_CONFIRMED, msgType, pTrans);
			}
			else
			{
				logError("received unexpected sipMsgType: SIP_TRANS_SIPMSG_RESPONSE in IS state COMPLETED, ignore.");
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TIMEOUT:
		{
			sipTransaction_t* pTrans = pMsg;
            if(pTrans->sipTransISTimer.timerIdG == timerId)
            {
                pTrans->sipTransISTimer.timerIdG = 0;
                osMem_deref(pTrans);
                logInfo("timer G expires, retransmit the request.");

                status = sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);
                if(status != OS_STATUS_OK)
                {
                    logError("fails to send request.");
                    sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    goto EXIT;
                }

                if(pTrans->isUDP)
                {
                    pTrans->timerAEGValue = osMinInt(pTrans->timerAEGValue*2, SIP_TIMER_T2);
                    pTrans->sipTransISTimer.timerIdG = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                }
            }
            else if(pTrans->sipTransISTimer.timerIdH == timerId)
            {
                pTrans->sipTransISTimer.timerIdH = 0;
                osMem_deref(pTrans);
                logInfo("timer H expires, terminate the transaction.");

                if(pTrans->sipTransISTimer.timerIdG !=0)
                {
                    osStopTimer(pTrans->sipTransISTimer.timerIdG);
                    pTrans->sipTransISTimer.timerIdG = 0;
                    osMem_deref(pTrans);
                }

                sipTU_onMsg(msgType, pTrans);

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			}
			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			sipTransaction_t* pTrans = pMsg;

			sipTU_onMsg(msgType, pTrans);
			sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			break;
		}
		default:
			logInfo("unexpected msgType (%d) received, ignore.", msgType);
			goto EXIT;
			break;
	}	

EXIT:
    return status;
}



osStatus_e sipTransISStateConfirmed_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    switch(msgType)
    {
        case SIP_TRANS_MSG_TYPE_TIMEOUT:
		{
			sipTransaction_t* pTrans = pMsg;
        	if(pTrans->sipTransISTimer.timerIdI == timerId)
            {
                pTrans->sipTransISTimer.timerIdI = 0;
				osMem_deref(pTrans);
                debug("timer J expires, terminate the transaction.");

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
                goto EXIT;
            }
            else
            {
                logError("receive an unexpected timerId (%d), ignore.", timerId);
                goto EXIT;
            }
			break;
		}
		default:
            logInfo("unexpected msgType (%d) received, ignore.", msgType);
            goto EXIT;
            break;
    }

EXIT:
	return status;
}


osStatus_e sipTransISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
    osStatus_e status = OS_STATUS_OK;

    switch (newState)
    {
        case SIP_TRANS_STATE_PROCEEDING:
            pTrans->state = newState;

			//build a 100 TRYING and send
			osMBuf_dealloc(pTrans->pResp);
			pTrans->pResp = sipTransIS_build100Trying(pTrans->pReq);
            sipTransport_send(SIP_TRANS_SIPMSG_RESPONSE, pTrans);

			sipTU_onMsg(msgType, pTrans);

			break;
		case SIP_TRANS_STATE_COMPLETED:	
            pTrans->sipTransISTimer.timerIdH = sipTransStartTimer(SIP_TIMER_H, osMem_ref(pTrans));
            if(pTrans->sipTransISTimer.timerIdH == 0)
            {
                logError("fails to start timerH.");
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(pTrans->isUDP)
            {
                pTrans->timerAEGValue = osMinInt(pTrans->timerAEGValue*2, SIP_TIMER_T2);
                pTrans->sipTransISTimer.timerIdG = sipTransStartTimer(pTrans->timerAEGValue, osMem_ref(pTrans));
                if(pTrans->sipTransISTimer.timerIdG == 0)
                {
                    logError("fails to start timerG.");
                    sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }
            pTrans->state = newState;

            break;
        case SIP_TRANS_STATE_CONFIRMED:
            pTrans->state = newState;
			if(pTrans->isUDP)
			{
            	pTrans->sipTransISTimer.timerIdI = sipTransStartTimer(SIP_TIMER_J, osMem_ref(pTrans));
			}
			else
			{
				sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			}
			break;
        case SIP_TRANS_STATE_TERMINATED:
            switch(msgType)
            {
				case SIP_TRANS_MSG_TYPE_TIMEOUT:
                case SIP_TRANS_MSG_TYPE_TX_FAILED:
                case SIP_TRANS_MSG_TYPE_INTERNAL_ERROR:
				case SIP_TRANS_MSG_TYPE_PEER:
					pTrans->state = newState;
					break;
				default:
					logError("received unexpected msgType (%d), this shall never happen.", msgType);
					break;
			}

            //clean up the transaction.
            if(pTrans->sipTransISTimer.timerIdG != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdG);
                pTrans->sipTransISTimer.timerIdG = 0;
				osMem_deref(pTrans);
            }
            if(pTrans->sipTransISTimer.timerIdH != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdH);
                pTrans->sipTransISTimer.timerIdH = 0;
                osMem_deref(pTrans);
            }
            if(pTrans->sipTransISTimer.timerIdI != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdI);
                pTrans->sipTransISTimer.timerIdI = 0;
                osMem_deref(pTrans);
            }

            osHash_deleteElement(pTrans->pTransHashLE);
            osHashData_t* pHashData = pTrans->pTransHashLE->data;
            osMem_deref((sipTransaction_t*)pHashData->pData);
            free(pTrans->pTransHashLE);

            break;
        default:
            logError("received unexpected newState (%d).", newState);
            status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
    return status;
}


static osMBuf_t* sipTransIS_build100Trying(osMBuf_t* pSipBufReq)
{
	osStatus_e status = OS_STATUS_OK;
	
	sipMsgResponse_t* pResp = NULL;
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = NULL;

	if(!pSipBufReq)
	{
		logError("null pointer, pSipBufReq.");
		goto EXIT;
	}

	osMBuf_t* pSipBufResp = osMBuf_alloc(SIP_MAX_MSG_SIZE);
	if(!pSipBufResp)
	{
		logError("fails to allocate pSipBufResp.");
		goto EXIT;
	}

	sipHdrName_e sipHdrArray[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
	int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
	pReqDecodedRaw = sipDecodeMsgRawHdr(pSipBufReq, sipHdrArray, arraySize);
	if(!pReqDecodedRaw)
	{
		logError("fails to sipDecodeMsgRawHdr.");
		goto EXIT;
	}

    //encode status line
	sipResponse_e rspCode = SIP_RESPONSE_100;
    status = sipHdrFirstline_respEncode(pSipBufResp, &rspCode, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to encode the response, respCode=%d.", rspCode);
        goto EXIT;
    }

	sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
	for(int i=0; i<arraySize; i++)
	{
		status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], &pReqDecodedRaw->msgHdrList[sipHdrArray[i]]->rawHdrList, NULL, ctrl);
		if(status != OS_STATUS_OK)
	    {
    	    logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
        	goto EXIT;
    	}
	}

    uint32_t contentLen = 0;
    sipHdrAddCtrl_t ctrlCL = {false, true, false, NULL};
    status = sipMsgAddHdr(pResp->sipMsg, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
	
EXIT:
	osMem_deref(pReqDecodedRaw);

	return pSipBufResp;
}
	
