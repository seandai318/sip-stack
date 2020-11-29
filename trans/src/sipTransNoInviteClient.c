/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTransNoInviteClient.c
 ********************************************************/

#include "osTimer.h"
#include "osMisc.h"
#include "osHash.h"
#include "osPreMemory.h"

#include "sipMsgRequest.h"
#include "sipConfig.h"
#include "sipTransMgr.h"
#include "sipTransIntf.h"
#include "sipTUIntf.h"
#include "sipTransportIntf.h"
#include "sipCodecUtil.h"


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
			pTrans->appType = ((sipTransMsg_t*)pMsg)->appType;
		}
	}
	if(msgType == SIP_TRANS_MSG_TYPE_TX_FAILED || msgType == SIP_TRANS_MSG_TYPE_TX_TCP_READY)
	{
		pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;
        if(!pTrans)
        {
            logError("null pointer, pTrans, for msgType (%d).", msgType);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
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
            logError("received a event (msgType=%d) that has invalid transaction state (%d),", msgType, (sipTransaction_t*)pTrans->state);
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

    if(((sipTransMsg_t*) pMsg)->sipMsgType != SIP_MSG_REQUEST)
    {
        logError("received a unexpected response.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;
	pTrans->appType = ((sipTransMsg_t*)pMsg)->appType;

	//shall not use request.pTransInfo->transId.viaId.host/port here, as this is the request top via's host/ip, which is own host/ip
	pTrans->tpInfo.peer = ((sipTransMsg_t*)pMsg)->request.sipTrMsgBuf.tpInfo.peer;
	pTrans->tpInfo.local = ((sipTransMsg_t*)pMsg)->request.sipTrMsgBuf.tpInfo.local;
	pTrans->tpInfo.protocolUpdatePos = ((sipTransMsg_t*)pMsg)->request.sipTrMsgBuf.tpInfo.protocolUpdatePos;

	pTrans->tpInfo.tpType = TRANSPORT_TYPE_ANY;
    pTrans->tpInfo.tcpFd = -1;

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
        logError("null pointer, pMsg.");
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

				sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
	            if(tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_TCP_FAIL)
    	        {
        	        logError("fails to send request.");
            	    sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                	status = OS_ERROR_INVALID_VALUE;
                	goto EXIT;
            	}

	        	if(tpStatus == TRANSPORT_STATUS_UDP)
    	    	{
					pTrans->tpInfo.tpType = TRANSPORT_TYPE_UDP;
        	    	pTrans->timerAEGValue = osMinInt(pTrans->timerAEGValue*2, SIP_TIMER_T2);
            		pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
        		}
				else
				{
					pTrans->tpInfo.tpType = TRANSPORT_TYPE_TCP;
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
    		if(((sipTransMsg_t*) pMsg)->sipMsgType == SIP_TRANS_MSG_CONTENT_REQUEST)
    		{
        		logError("received a SIP request from peer while in NIC.");
        		goto EXIT;
    		}

			sipTransaction_t* pTrans = ((sipTransMsg_t*) pMsg)->pTransId;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;
			osfree(pTrans->resp.pSipMsg);
			pTrans->resp = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf;

			sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
			pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_MESSAGE;
			pSipTUMsg->sipMsgType = SIP_MSG_RESPONSE;
			sipMsgBuf_copy(&pSipTUMsg->sipMsgBuf, &pTrans->resp);
			pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
			pSipTUMsg->pTUId = pTrans->pTUId;

logError("to-remove, TCP, rspCode=%d", rspCode);
    		if(rspCode >= 100 && rspCode < 200)
    		{
				sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg);
				osfree(pSipTUMsg);

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

				sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg);
				osfree(pSipTUMsg);

				sipTransNICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
		{
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
			pTrans->tpInfo.tpType = TRANSPORT_TYPE_TCP;
			pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*) pMsg)->tcpFd;

			sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
			if(tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_TCP_FAIL)
            {
                logError("fails to send request.");
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                status = OS_ERROR_NETWORK_FAILURE;
                goto EXIT;
            }

			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");

            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
            status = OS_ERROR_NETWORK_FAILURE;
            goto EXIT;
			break;
		}
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

                sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
                if(tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_TCP_FAIL)
                {
                    logError("fails to send request.");
                    sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }

				//this is a redundant check as timeout implies the UDP is used
                if(tpStatus == TRANSPORT_STATUS_UDP)
                {
                    pTrans->tpInfo.tpType = TRANSPORT_TYPE_UDP;
                    pTrans->timerAEGValue = SIP_TIMER_T2;
                    pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                }
				else
				{
                    pTrans->tpInfo.tpType = TRANSPORT_TYPE_TCP;
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
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;
			osfree(pTrans->resp.pSipMsg);
			pTrans->resp = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf;

            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_MESSAGE;
            pSipTUMsg->sipMsgType = SIP_MSG_RESPONSE;
            sipMsgBuf_copy(&pSipTUMsg->sipMsgBuf, &pTrans->resp);
            pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;

            if(rspCode >= 100 && rspCode < 200)
            {
                sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg);
				osfree(pSipTUMsg);
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

                sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg);
				osfree(pSipTUMsg);

                sipTransNICEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
        {
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            pTrans->tpInfo.tpType = TRANSPORT_TYPE_TCP;
            pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*) pMsg)->tcpFd;

            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_TCP_FAIL)
            {
                logError("fails to send request.");
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
            break;
        }
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");

            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
            status = OS_ERROR_NETWORK_FAILURE;
            goto EXIT;
            break;
		}
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
	sipTransportStatus_e tpStatus;
	sipTransState_e curState = pTrans->state;
	pTrans->state = newState;

	debug("curState=%d, newState=%d", curState, newState);
    switch (newState)
    {
        case SIP_TRANS_STATE_TRYING:
            tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->req.pSipMsg);
			if(tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_TCP_FAIL)
			{
                logError("fails to send SIP message out, tpStatus=%d.", tpStatus);
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
			}

            pTrans->sipTransNICTimer.timerIdF = sipTransStartTimer(SIP_TIMER_F, pTrans);
            if(pTrans->sipTransNICTimer.timerIdF == 0)
            {
                logError("fails to start timerF.");
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(tpStatus == TRANSPORT_STATUS_UDP)
            {
                pTrans->tpInfo.tpType = TRANSPORT_TYPE_UDP;

                pTrans->timerAEGValue = SIP_TIMER_T1;
                pTrans->sipTransNICTimer.timerIdE = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                if(pTrans->sipTransNICTimer.timerIdE == 0)
                {
                    logError("fails to start timerE.");
                    sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }
			else
			{
                pTrans->tpInfo.tpType = TRANSPORT_TYPE_TCP;
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

            if(pTrans->tpInfo.tpType != TRANSPORT_TYPE_TCP)
            {
                pTrans->sipTransNICTimer.timerIdK = sipTransStartTimer(SIP_TIMER_K, pTrans);
            }
            else
            {
                sipTransNICEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
            }
            break;
        case SIP_TRANS_STATE_TERMINATED:
		{
            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;
		//	pSipTUMsg->pSipMsgBuf = &pTrans->req;
		//	pSipTUMsg->sipMsgType = SIP_MSG_RESPONSE;

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

                    //notify TU
					pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
					pSipTUMsg->errorInfo.isServerTransaction = false;
                    sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg);
					osfree(pSipTUMsg);
                    break;
                case SIP_TRANS_STATE_COMPLETED:
                    if(msgType == SIP_TRANS_MSG_TYPE_TX_FAILED)
                    {
                        //transport error
                    	pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
                    	pSipTUMsg->errorInfo.isServerTransaction = false;
                        sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg);
						osfree(pSipTUMsg);
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

            osHash_deleteNode(pTrans->pTransHashLE, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
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

