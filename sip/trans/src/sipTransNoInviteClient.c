#include "osTimer.h"
#include "osMisc.h"
#include "osHash.h"

#include "sipMsgRequest.h"
#include "sipConfig.h"
#include "sipTransMgr.h"
#include "sipTransIntf.h"
#include "sipTUIntf.h"
#include "sipTransport.h"


static osStatus_e sipTransNICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransNICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNICStateTrying_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNICStateCalling_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);


osStatus_e sipTransNoInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
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
		pTrans = ((sipTransMsg_t*)pMsg)->pTransId;
	    if(!pTrans)
    	{
        	logError("null pointer, pTrans, for msgType (%d).", msgType);
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    	}

		if(msgType == SIP_TRANS_MSG_TYPE_TU)
		{
			pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;
		}
	}
	
    switch(pTrans->state)
	{
        case SIP_TRANS_STATE_NONE:
            status = sipTransNICStateNone_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_TRYING:
            status = sipTransNICStateTrying_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_PROCEEDING:
            status = sipTransNICStateProceeding_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_COMPLETED:
            status = sipTransNICStateCompleted_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_TERMINATED:
        default:
            logError("received a timeout event that has invalid transaction state (%d),", (sipTransaction_t*)pTrans->state);
            status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
	DEBUG_END
    return status;
}


osStatus_e sipTransNICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(msgType != SIP_TRANS_MSG_TYPE_TU)
    {
        logError("receive unexpected msgType (%d) in NICStateNone.", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    sipTransaction_t* pTrans = ((sipTransMsg_t*) pMsg)->pTransId;
    if(!pTrans)
    {
        logError("null pointer, pTrans, for msgType (%d).", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;

	if(((sipTransMsg_t*) pMsg)->sipMsgType != SIP_MSG_REQUEST)
	{
		logError("received a unexpected response.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    sipTransNICEnterState(SIP_TRANS_STATE_TRYING, msgType, pTrans);
	
EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNICStateTrying_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
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

		    if(pTrans->sipTransNICTimer.timerIdE == timerId)
    		{
        		pTrans->sipTransNICTimer.timerIdE = 0;
                logInfo("timer E expires, resend the request.");

				status = sipTransport_send(SIP_MSG_REQUEST, pTrans);
        		if(status != OS_STATUS_OK)
        		{
            		logError("fails to send request.");
					sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
					goto EXIT;
				}

	        	if(pTrans->isUDP)
    	    	{
        	    	pTrans->timerAEGValue = osMinInt(pTrans->timerAEGValue*2, SIP_TIMER_T2);
            		pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
        		}
    		}
    		else if(pTrans->sipTransNICTimer.timerIdF == timerId)
    		{
        		pTrans->sipTransNICTimer.timerIdF = 0;
        		logInfo("timer F expires, terminate the transaction.");

                if(pTrans->sipTransNICTimer.timerIdE !=0)
                {
                    osStopTimer(pTrans->sipTransNICTimer.timerIdE);
                    pTrans->sipTransNICTimer.timerIdE = 0;
                }

				sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
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
    		if(((sipTransMsg_t*) pMsg)->sipMsgType == SIP_MSG_REQUEST)
    		{
        		logError("received a request from peer while in NIC.");
        		goto EXIT;
    		}

			sipTransaction_t* pTrans = ((sipTransMsg_t*) pMsg)->pTransId;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->pTransInfo->rspCode;
			osMem_deref(pTrans->resp.pSipMsg);
			pTrans->resp = ((sipTransMsg_t*)pMsg)->sipMsgBuf;

			sipTUMsg_t sipTUMsg;
			sipTUMsg.sipMsgType = SIP_MSG_RESPONSE;
			sipTUMsg.pSipMsgBuf = &pTrans->resp;
			sipTUMsg.pTransId = pTrans;
			sipTUMsg.pTUId = pTrans->pTUId;

    		if(rspCode >= 100 && rspCode < 200)
    		{
				sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);
				sipTransNICEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
			}
			else
			{
				if(pTrans->sipTransNICTimer.timerIdE != 0)
				{
					osStopTimer(pTrans->sipTransNICTimer.timerIdE);
					pTrans->sipTransNICTimer.timerIdE = 0;
				}
				if(pTrans->sipTransNICTimer.timerIdF != 0)
                {
                    osStopTimer(pTrans->sipTransNICTimer.timerIdF);
                    pTrans->sipTransNICTimer.timerIdF = 0;
                }

				sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);
				sipTransNICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_FAILED:
			logInfo("fails to tx msg, let the timer to perform retransmission, ignore.");
			break;
		default:
			logError("received unexpected msgtype (%d), ignore.", msgType);
			break;
	}
 
EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipTransaction_t* pTrans = NULL;
    switch(msgType)
    {
        case SIP_TRANS_MSG_TYPE_TIMEOUT:
			pTrans = pMsg;
            if(pTrans->sipTransNICTimer.timerIdE == timerId)
            {
                pTrans->sipTransNICTimer.timerIdE = 0;
                logInfo("timer E expires, resend the request.");

                status = sipTransport_send(SIP_MSG_REQUEST, pTrans);
                if(status != OS_STATUS_OK)
                {
                    logError("fails to send request.");
                    sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    goto EXIT;
                }

                if(pTrans->isUDP)
                {
                    pTrans->timerAEGValue = SIP_TIMER_T2;
                    pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                }
            }
            else if(pTrans->sipTransNICTimer.timerIdF == timerId)
            {
                pTrans->sipTransNICTimer.timerIdF = 0;
                logInfo("timer F expires, terminate the transaction.");

				if(pTrans->sipTransNICTimer.timerIdE !=0)
				{
					osStopTimer(pTrans->sipTransNICTimer.timerIdE);
					pTrans->sipTransNICTimer.timerIdE = 0;
				}

                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
                goto EXIT;
            }
            else
            {
                logError("receive an unexpected timerId (%d), ignore.", timerId);
                goto EXIT;
            }
            break;
        case SIP_TRANS_MSG_TYPE_PEER:
		{
            pTrans = ((sipTransMsg_t*) pMsg)->pTransId;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->pTransInfo->rspCode;
			osMem_deref(pTrans->resp.pSipMsg);
			pTrans->resp = ((sipTransMsg_t*)pMsg)->sipMsgBuf;

            sipTUMsg_t sipTUMsg;
            sipTUMsg.sipMsgType = SIP_MSG_RESPONSE;
            sipTUMsg.pSipMsgBuf = &pTrans->resp;
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;

            if(rspCode >= 100 && rspCode < 200)
            {
                sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);
            }
            else
            {
                if(pTrans->sipTransNICTimer.timerIdE != 0)
                {
                    osStopTimer(pTrans->sipTransNICTimer.timerIdE);
                    pTrans->sipTransNICTimer.timerIdE = 0;
                }
                if(pTrans->sipTransNICTimer.timerIdF != 0)
                {
                    osStopTimer(pTrans->sipTransNICTimer.timerIdF);
                    pTrans->sipTransNICTimer.timerIdF = 0;
                }

                sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);
                sipTransNICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
            logInfo("fails to tx msg, let the timer to perform retransmission, ignore.");
            break;
        default:
            logError("received unexpected msgtype (%d), ignore.", msgType);
            break;
    }

EXIT:
	DEBUG_END
    return status;
}



osStatus_e sipTransNICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
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
        	if(pTrans->sipTransNICTimer.timerIdK == timerId)
            {
                pTrans->sipTransNICTimer.timerIdK = 0;
                debug("timer K expires, terminate the transaction.");

                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
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
			logInfo("receive unexpected msgType (%d), ignore.", msgType);
			goto EXIT;
	}

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	sipTransState_e curState = pTrans->state;
	pTrans->state = newState;

    switch (newState)
    {
        case SIP_TRANS_STATE_TRYING:
            sipTransport_send(SIP_MSG_REQUEST, pTrans);

            pTrans->sipTransNICTimer.timerIdF = sipTransStartTimer(SIP_TIMER_F, osMem_ref(pTrans));
            if(pTrans->sipTransNICTimer.timerIdF == 0)
            {
                logError("fails to start timerF.");
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(pTrans->isUDP)
            {
                pTrans->timerAEGValue = SIP_TIMER_T1;
                pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, osMem_ref(pTrans));
                if(pTrans->sipTransNICTimer.timerIdE == 0)
                {
                    logError("fails to start timerE.");
                    sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }
            break;
        case SIP_TRANS_STATE_PROCEEDING:
            break;
        case SIP_TRANS_STATE_COMPLETED:
            if(pTrans->sipTransNICTimer.timerIdE !=0)
            {
                osStopTimer(pTrans->sipTransNICTimer.timerIdE);
                pTrans->sipTransNICTimer.timerIdE = 0;
            }
            if(pTrans->sipTransNICTimer.timerIdF !=0)
            {
                osStopTimer(pTrans->sipTransNICTimer.timerIdF);
                pTrans->sipTransNICTimer.timerIdF = 0;
            }

            if(pTrans->isUDP)
            {
                sipTransStartTimer(SIP_TIMER_K, pTrans);
            }
            else
            {
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
            }
            break;
        case SIP_TRANS_STATE_TERMINATED:
		{
            sipTUMsg_t sipTUMsg = {};
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;

            switch(curState)
            {
                case SIP_TRANS_STATE_TRYING:
                case SIP_TRANS_STATE_PROCEEDING:
	                if(pTrans->sipTransNICTimer.timerIdE !=0)
    	            {
        	            osStopTimer(pTrans->sipTransNICTimer.timerIdE);
            	        pTrans->sipTransNICTimer.timerIdE = 0;
                	}
                    if(pTrans->sipTransNICTimer.timerIdF !=0)
                    {
                        osStopTimer(pTrans->sipTransNICTimer.timerIdF);
                        pTrans->sipTransNICTimer.timerIdF = 0;
                    }

                    //notify UT
                    sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);
                    break;
                case SIP_TRANS_STATE_COMPLETED:
                    if(msgType == SIP_TRANS_MSG_TYPE_TX_FAILED)
                    {
                        //transport error
                        sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);
                    }
                    break;
                default:
                    logError("from unexpected NIC state (%d) to enter NIC state SIP_TRANS_STATE_TERMINATED.", pTrans->state);
                    status = OS_ERROR_INVALID_VALUE;
                    break;
            }

            //clean up the transaction.
            if(pTrans->sipTransNICTimer.timerIdK != 0)
            {
                osStopTimer(pTrans->sipTransNICTimer.timerIdK);
                pTrans->sipTransNICTimer.timerIdK = 0;
            }

            osHash_deleteElement(pTrans->pTransHashLE);
            osHashData_t* pHashData = pTrans->pTransHashLE->data;
            osMem_deref((sipTransaction_t*)pHashData->pData);
            free(pTrans->pTransHashLE);

            break;
		}
        default:
            logError("received unexpected newState (%d).", newState);
            status = OS_ERROR_INVALID_VALUE;
            break;

    }

EXIT:
	DEBUG_END
    return status;
}

