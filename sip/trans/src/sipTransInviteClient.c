#include "osTimer.h"
#include "osHash.h"
#include "osPreMemory.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipTransIntf.h"
#include "sipTransMgr.h"
#include "sipTUIntf.h"
#include "sipTransportIntf.h"


static osStatus_e sipTransICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateCalling_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);


osStatus_e sipTransInviteClient_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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

		if( msgType == SIP_TRANS_MSG_TYPE_TU)
		{
			pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;
		}
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
	DEBUG_END
    return status;
}


osStatus_e sipTransICStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	osStatus_e status = OS_STATUS_OK;
	DEBUG_BEGIN

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

    sipTransaction_t* pTrans = ((sipTransMsg_t*) pMsg)->pTransId;
    if(!pTrans)
    {
        logError("null pointer, pTrans, for msgType (%d).", msgType);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
	pTrans->pTUId = ((sipTransMsg_t*) pMsg)->pSenderId;

	if(!((sipTransMsg_t*) pMsg)->sipMsgType != SIP_MSG_REQUEST)
	{
		logError("received a unexpected response.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pTrans->tpInfo = ((sipTransMsg_t*)pMsg)->request.sipTrMsgBuf.tpInfo;
    pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_ANY;
    pTrans->tpInfo.tcpFd = -1;

    sipTransICEnterState(SIP_TRANS_STATE_CALLING, msgType, pTrans);
	
EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransICStateCalling_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;
	DEBUG_BEGIN

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

				pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_UDP;
            	sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
            	if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
            	{
                	logError("fails to send request.");
                	sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                	status = OS_ERROR_NETWORK_FAILURE;
                	goto EXIT;
            	}

        	    pTrans->timerAEGValue = pTrans->timerAEGValue*2;
            	pTrans->sipTransICTimer.timerIdA = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
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

	            sipTUMsg_t sipTUMsg;
    	        sipTUMsg.pTransId = pTrans;
        	    sipTUMsg.pTUId = pTrans->pTUId;
				sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);

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
			sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;

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

			sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;	
			osMem_deref(pTrans->resp.pSipMsg);
            pTrans->resp = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf;

            sipTUMsg_t sipTUMsg;
            sipTUMsg.sipMsgType = SIP_MSG_RESPONSE;
            sipTUMsg.pSipMsgBuf = &pTrans->resp;
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;
            sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);

    		if(rspCode >= 100 && rspCode < 200)
    		{
				sipTransICEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
			}
			else if(rspCode >= 200 && rspCode < 300)
			{
				sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			}
			else
			{
                //to-do, build ACK, and store in pTrans, per 3261, the ACK by transaction has to be sent to the same peer IP/port/transport, the SIP_TRANSPORT_STATUS_TCP_CONN case shall not occur
                sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->ack.pSipMsg);
                if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
                {
                    logError("fails to send request.");
                    //notify TU about the ACK failure, to-do
                    sipTUMsg.sipMsgType = SIP_MSG_ACK;
                    sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);

                    sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    status = OS_ERROR_NETWORK_FAILURE;
                    goto EXIT;
                }
                else if(tpStatus == SIP_TRANSPORT_STATUS_UDP)
                {
                    pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_UDP;
                    pTrans->sipTransICTimer.timerIdD = sipTransStartTimer(SIP_TIMER_D, pTrans);
                    sipTransICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
                }
                else if(tpStatus == SIP_TRANSPORT_STATUS_TCP_OK)
                {
                    sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    goto EXIT;
                }
			}
			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
        {
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
            pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*) pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
            if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
            {
                logError("fails to send request.");
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                status = OS_ERROR_NETWORK_FAILURE;
                goto EXIT;
            }
            break;
        }
		case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");

            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
            status = OS_ERROR_NETWORK_FAILURE;
            goto EXIT;
			break;
		}
		default:
			logError("received unexpected msgType (%d), ignore.", msgType);
			break;
	}
 
EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransICStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
    osStatus_e status = OS_STATUS_OK;
	DEBUG_BEGIN

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
            sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;
            osMem_deref(pTrans->resp.pSipMsg);
            pTrans->resp = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf;

            sipTUMsg_t sipTUMsg;
            sipTUMsg.sipMsgType = SIP_MSG_RESPONSE;
            sipTUMsg.pSipMsgBuf = &pTrans->resp;
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;

            sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);

            if(rspCode >= 100 && rspCode < 200)
            {
                //stay in the same state
            }
            else if(rspCode >= 200 && rspCode < 300)
            {
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
            else
            {
                //to-do, build ACK, and store in pTrans, per 3261, the ACK by transaction has to be sent to the same peer IP/port/transport, the SIP_TRANSPORT_STATUS_TCP_CONN case shall not occur
	            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->ack.pSipMsg);
    	        if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
        	    {
            	    logError("fails to send request.");
					//notify TU about the ACK failure, to-do
					sipTUMsg.sipMsgType = SIP_MSG_ACK;
                	sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, &sipTUMsg);

                	sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                	status = OS_ERROR_NETWORK_FAILURE;
                	goto EXIT;
            	}
				else if(tpStatus == SIP_TRANSPORT_STATUS_UDP)
				{
					pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_UDP; 
            		pTrans->sipTransICTimer.timerIdD = sipTransStartTimer(SIP_TIMER_D, pTrans);
                	sipTransICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
				}
				else if(tpStatus == SIP_TRANSPORT_STATUS_TCP_OK)
				{
					sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
					goto EXIT;
				}
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;

            sipTUMsg_t sipTUMsg;
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;

			sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);
			sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			break;
		}
		default:
			logInfo("unexpected msgType (%d) received, ignore.", msgType);
			goto EXIT;
			break;
	}	

EXIT:
	DEBUG_END
    return status;
}



osStatus_e sipTransICStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
            sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;
            osMem_deref(pTrans->resp.pSipMsg);
            pTrans->resp = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf;
            if(rspCode >= 300 && rspCode < 699)
            {
                //ACK must have been built, reuse
                sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->ack.pSipMsg);
                if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
                {
                    logError("fails to send request.");
                    sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    status = OS_ERROR_NETWORK_FAILURE;
                    goto EXIT;
                }
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;

            sipTUMsg_t sipTUMsg;
            sipTUMsg.pTransId = pTrans;
            sipTUMsg.pTUId = pTrans->pTUId;

            sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);
            sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            break;
		}
        default:
            logInfo("unexpected msgType (%d) received, ignore.", msgType);
            goto EXIT;
            break;
    }

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransICEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	sipTransState_e prvState = pTrans->state;
    pTrans->state = newState;
	
    switch (newState)
    {
        case SIP_TRANS_STATE_CALLING:
		{
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
            if(tpStatus == SIP_TRANSPORT_STATUS_FAIL || tpStatus == SIP_TRANSPORT_STATUS_TCP_FAIL)
			{
				logError("fails to start timerE.");
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                status = OS_ERROR_NETWORK_FAILURE;
                goto EXIT;
            }

            pTrans->sipTransICTimer.timerIdB = sipTransStartTimer(SIP_TIMER_B, pTrans);
            if(pTrans->sipTransICTimer.timerIdB == 0)
            {
                logError("fails to start timerB.");
                sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(tpStatus == SIP_TRANSPORT_STATUS_UDP)
            {
                pTrans->timerAEGValue = SIP_TIMER_T1;
                pTrans->sipTransICTimer.timerIdA = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                if(pTrans->sipTransICTimer.timerIdA == 0)
                {
                    logError("fails to start timerA.");
                    sipTransICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }
            break;
		}
        case SIP_TRANS_STATE_PROCEEDING:
        case SIP_TRANS_STATE_COMPLETED:
			break;
        case SIP_TRANS_STATE_TERMINATED:
            switch(msgType)
            {
				case SIP_TRANS_MSG_TYPE_TIMEOUT:
					break;
                case SIP_TRANS_MSG_TYPE_TX_FAILED:
                case SIP_TRANS_MSG_TYPE_INTERNAL_ERROR:
				{
            		sipTUMsg_t sipTUMsg;
            		sipTUMsg.pTransId = pTrans;
            		sipTUMsg.pTUId = pTrans->pTUId;

                    sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, &sipTUMsg);
					break;
				}
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

            osHash_deleteNode(pTrans->pTransHashLE);
            osHashData_t* pHashData = pTrans->pTransHashLE->data;
            osMem_deref((sipTransaction_t*)pHashData->pData);
            osfree(pTrans->pTransHashLE);

            break;
        default:
            logError("received unexpected newState (%d).", newState);
            status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
	DEBUG_END
    return status;
}

