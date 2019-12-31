#include "osTimer.h"
#include "osHash.h"

#include "sipConfig.h"
#include "sipTransaction.h"
#include "sipMsgRequest.h"
#include "sipTransaction.h"
#include "sipTransMgr.h"
#include "sipTU.h"
#include "sipTransport.h"


static osStatus_e sipTransICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateCalling_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);


osStatus_e sipTransNoInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
            status = sipTransICStateNone_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_CALLING:
            status = sipTransICStateCalling_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_PROCEEDING:
            status = sipTransICStateProceeding_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_COMPLETED:
            status = sipTransICStateCompleted_onMsg(msgType, pMsg, timerId);
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


osStatus_e sipTransICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(msgType != SIP_TRANS_MSG_TYPE_TU)
    {
        logError("receive unexpected msgType (%d) in ICStateNone.", msgType);
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

	if(!((sipTransMsg2SM_t*) pMsg)->sipMsgType != SIP_TRANS_SIPMSG_REQUEST)
	{
		logError("received a unexpected response.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

//    sipTransport_send(pTrans->pReq, pTrans);

    sipTransICEnterState(SIP_TRANS_STATE_CALLING, msgType, pTrans);
	
EXIT:
	return status;
}


osStatus_e sipTransICStateCalling_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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

		    if(pTrans->sipTransICTimer.timerIdA == timerId)
    		{
        		pTrans->sipTransICTimer.timerIdA = 0;
                logInfo("timer A expires, retransmit the request.");

				status = sipTransport_send(SIP_TRANS_SIPMSG_REQUEST, pTrans);
        		if(status != OS_STATUS_OK)
        		{
            		logError("fails to send request.");
					sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
					goto EXIT;
				}

	        	if(pTrans->isUDP)
    	    	{
        	    	pTrans->timerAEGValue = pTrans->timerAEGValue*2;
            		pTrans->sipTransICTimer.timerIdA = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
        		}
    		}
    		else if(pTrans->sipTransICTimer.timerIdB == timerId)
    		{
        		pTrans->sipTransICTimer.timerIdB = 0;
        		logInfo("timer B expires, terminate the transaction.");

                if(pTrans->sipTransICTimer.timerIdA !=0)
                {
                    osStopTimer(pTrans->sipTransICTimer.timerIdA);
                    pTrans->sipTransICTimer.timerIdA = 0;
                }

				sipTU_onMsg(msgType, pTrans);

				sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
        		goto EXIT;
    		}
    		else
    		{
        		logError("receive an unexpected timerId (%d), ignore.", timerId);
        		goto EXIT;
    		}
			break;
		}
		case SIP_TRANS_MSG_TYPE_PEER:
		{
			sipTransaction_t* pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;

            if(pTrans->sipTransICTimer.timerIdA !=0)
            {
                osStopTimer(pTrans->sipTransICTimer.timerIdA);
                pTrans->sipTransICTimer.timerIdA = 0;
            }
            if(pTrans->sipTransICTimer.timerIdB !=0)
            {
                osStopTimer(pTrans->sipTransICTimer.timerIdB);
                pTrans->sipTransICTimer.timerIdB = 0;
            }

			sipResponse_e rspCode = ((sipTransMsg2SM_t*)pMsg)->pTransInfo->rspCode;	
			osMem_deref(pTrans->pResp);
            pTrans->pResp = ((sipTransMsg2SM_t*)pMsg)->pSipMsg;
    		if(rspCode >= 100 && rspCode < 200)
    		{
				sipTU_onMsg(msgType, pTrans);
				sipTransICEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
			}
			else if(rspCode >= 200 && rspCode < 300)
			{
				sipTU_onMsg(msgType, pTrans);
				sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			}
			else
			{
				//to-do, build ACK, and store in pTrans
				sipTransport_send(SIP_TRANS_SIPMSG_ACK, pTrans);

				sipTU_onMsg(msgType, pTrans);
				sipTransICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_FAILED:
			logInfo("fails to tx msg, let the timer to perform retransmission, ignore.");
			break;
		default:
			logError("received unexpected msgType (%d), ignore.", msgType);
			break;
	}
 
EXIT:
	return status;
}


osStatus_e sipTransICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
            sipResponse_e rspCode = ((sipTransMsg2SM_t*)pMsg)->pTransInfo->rspCode;
            osMem_deref(pTrans->pResp);
            pTrans->pResp = ((sipTransMsg2SM_t*)pMsg)->pSipMsg;
            if(rspCode >= 100 && rspCode < 200)
            {
                sipTU_onMsg(msgType, pTrans);
            }
            else if(rspCode >= 200 && rspCode < 300)
            {
                sipTU_onMsg(msgType, pTrans);
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
            else
            {
                //to-do, build ACK, and store in pTrans
                sipTransport_send(SIP_TRANS_SIPMSG_ACK, pTrans);

                sipTU_onMsg(msgType, pTrans);
                sipTransICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			sipTransaction_t* pTrans = pMsg;
			sipTU_onMsg(msgType, pTrans);
			sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
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



osStatus_e sipTransICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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

        	if(pTrans->sipTransICTimer.timerIdD == timerId)
            {
                pTrans->sipTransICTimer.timerIdD = 0;
                debug("timer K expires, terminate the transaction.");

                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
                goto EXIT;
            }
            else
            {
                logError("receive an unexpected timerId (%d), ignore.", timerId);
                goto EXIT;
            }
			break;
		}
		case SIP_TRANS_MSG_TYPE_PEER:
		{
            sipTransaction_t* pTrans = ((sipTransMsg2SM_t*)pMsg)->pTrans;
            sipResponse_e rspCode = ((sipTransMsg2SM_t*)pMsg)->pTransInfo->rspCode;
            osMem_deref(pTrans->pResp);
            pTrans->pResp = ((sipTransMsg2SM_t*)pMsg)->pSipMsg;
            if(rspCode >= 300 && rspCode < 699)
            {
                //ACK must have been built, reuse
                sipTransport_send(SIP_TRANS_SIPMSG_ACK, pTrans);
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			sipTransaction_t* pTrans = pMsg;

            sipTU_onMsg(msgType, pTrans);
            sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
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


osStatus_e sipTransICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
    osStatus_e status = OS_STATUS_OK;

    switch (newState)
    {
        case SIP_TRANS_STATE_CALLING:
            sipTransport_send(SIP_TRANS_SIPMSG_REQUEST, pTrans);

            pTrans->sipTransICTimer.timerIdB = sipTransStartTimer(SIP_TIMER_B, osMem_ref(pTrans));
            if(pTrans->sipTransICTimer.timerIdB == 0)
            {
                logError("fails to start timerB.");
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(pTrans->isUDP)
            {
                pTrans->timerAEGValue = SIP_TIMER_T1;
                pTrans->sipTransICTimer.timerIdA = sipTransStartTimer(pTrans->timerAEGValue, osMem_ref(pTrans));
                if(pTrans->sipTransICTimer.timerIdA == 0)
                {
                    logError("fails to start timerA.");
                    sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }

            pTrans->state = newState;
            break;
        case SIP_TRANS_STATE_PROCEEDING:
        case SIP_TRANS_STATE_COMPLETED:
            pTrans->state = newState;
			break;
        case SIP_TRANS_STATE_TERMINATED:
            switch(msgType)
            {
				case SIP_TRANS_MSG_TYPE_TIMEOUT:
					pTrans->state = newState;
					break;
                case SIP_TRANS_MSG_TYPE_TX_FAILED:
                case SIP_TRANS_MSG_TYPE_INTERNAL_ERROR:
				case SIP_TRANS_MSG_TYPE_PEER:
                    sipTU_onMsg(msgType, pTrans);
					pTrans->state = newState;
					break;
				default:
					logError("received unexpected msgType (%d), this shall never happen.", msgType);
					break;
			}

            //clean up the transaction.
            if(pTrans->sipTransICTimer.timerIdD != 0)
            {
                osStopTimer(pTrans->sipTransICTimer.timerIdD);
                pTrans->sipTransICTimer.timerIdD = 0;
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

