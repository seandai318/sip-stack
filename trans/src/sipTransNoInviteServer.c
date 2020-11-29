/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTransNoInviteServer.c
 ********************************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "osTimer.h"
#include "osHash.h"
#include "osPreMemory.h"
#include "osDebug.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipTransMgr.h"
#include "sipTransIntf.h"
#include "sipTUIntf.h"
#include "sipTransportIntf.h"
#include "sipCodecUtil.h"


static osStatus_e sipTransNISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransNISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNISStateTrying_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransNISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);


osStatus_e sipTransNoInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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

		debug("pTrans->tpInfo.tcpFd=%d, pTrans->tpInfo.tpType=%d\n", pTrans->tpInfo.tcpFd, pTrans->tpInfo.tpType);
		if( msgType == SIP_TRANS_MSG_TYPE_TU)
		{
			pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;
			pTrans->appType = ((sipTransMsg_t*)pMsg)->appType;
		}
    }

    switch(pTrans->state)
	{
		case SIP_TRANS_STATE_NONE:
			status = sipTransNISStateNone_onMsg(msgType, pMsg, timerId);
			break;
        case SIP_TRANS_STATE_TRYING:
            status = sipTransNISStateTrying_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_PROCEEDING:
            status = sipTransNISStateProceeding_onMsg(msgType, pMsg, timerId);
            break;
        case SIP_TRANS_STATE_COMPLETED:
            status = sipTransNISStateCompleted_onMsg(msgType, pMsg, timerId);
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
	

osStatus_e sipTransNISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(msgType != SIP_TRANS_MSG_TYPE_PEER)
	{
        logError("receive unexpected msgType (%d) in NISStateNone.", msgType);
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

    debug("pTrans=%p, tpInfo (isCom=%d, tpType=%d, tcpFd=%d, peer=%A).", pTrans, pTrans->tpInfo.isCom, pTrans->tpInfo.tpType, pTrans->tpInfo.tcpFd, &pTrans->tpInfo.peer);

	sipTransNISEnterState(SIP_TRANS_STATE_TRYING, msgType, pTrans);

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNISStateTrying_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    debug("msgType=%d", msgType);
	switch(msgType)
	{
		case SIP_TRANS_MSG_TYPE_TU:
		{
			sipTransMsg_t* pTU = pMsg;
    		sipTransaction_t* pTrans = pTU->pTransId;
    		if(!pTrans)
    		{
        		logError("null pointer, pTrans, for msgType (%d).", msgType);
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}
			pTrans->pTUId = pTU->pSenderId;
            pTrans->appType = ((sipTransMsg_t*)pMsg)->appType;

			//the peer address from TU is the address in the top via, may be different from the real address used by the peer.  if the request message was received via TCP, the real peer has been saved when transaction was created, only reset when the message was received via UDP.
			if(pTrans->tpInfo.tcpFd < 0)
			{
				pTrans->tpInfo.peer = pTU->response.sipTrMsgBuf.tpInfo.peer;
			}

			pTrans->tpInfo.local = pTU->response.sipTrMsgBuf.tpInfo.local;
			if(pTU->sipMsgType != SIP_MSG_RESPONSE)
			{
				logError("received unexpected request message from TU in NISStateTrying, response message is expected.");
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}

			//pTrans->pResp shall be NULL before
			pTrans->resp = pTU->response.sipTrMsgBuf.sipMsgBuf;
			pTrans->resp.pSipMsg = osmemref(pTU->response.sipTrMsgBuf.sipMsgBuf.pSipMsg);

    		sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
			if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
			{
        		sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
				goto EXIT;
			}
			else if(tpStatus == TRANSPORT_STATUS_TCP_CONN)
			{
				pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
				goto EXIT;
			}
			else
			{
				pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_UDP;
			}

			if(pTU->response.rspCode >= 100 && pTU->response.rspCode <=199)
			{
        		sipTransNISEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
			}
    		else if(pTU->response.rspCode >= 200 && pTU->response.rspCode <= 699)
    		{
        		sipTransNISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
		{
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
			pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*)pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }

            if(pTrans->resp.rspCode >= 100 && pTrans->resp.rspCode <=199)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
            }
            else if(pTrans->resp.rspCode >= 200 && pTrans->resp.rspCode <= 699)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
		}
		default:
        	logError("receive unexpected msgType (%d) in NISStateTrying.", msgType);
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
			break;
	}

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    debug("msgType=%d", msgType);
	switch (msgType)
	{
		case SIP_TRANS_MSG_TYPE_TU:
		{
			sipTransMsg_t* pTU = pMsg;
    		if(pTU->sipMsgType != SIP_MSG_RESPONSE)
    		{
        		logError("received unexpected request message from TU in NISStateProceeding, response message is expected.");
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}

            sipTransaction_t* pTrans = pTU->pTransId;
		    if(!pTrans)
    		{
        		logError("null pointer, pTrans, for msgType (%d).", msgType);
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}
			pTrans->pTUId = pTU->pSenderId;
            pTrans->appType = pTU->appType;

			osfree(pTrans->resp.pSipMsg);
			pTrans->resp = pTU->response.sipTrMsgBuf.sipMsgBuf;
			pTrans->resp.pSipMsg = osmemref(pTU->response.sipTrMsgBuf.sipMsgBuf.pSipMsg);

            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }
            else if(tpStatus == TRANSPORT_STATUS_TCP_CONN)
            {
                pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
                goto EXIT;
            }
            else
            {
                pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_UDP;
            }


            if(pTU->response.rspCode >= 200 && pTU->response.rspCode <= 699)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
 		}
		case SIP_TRANS_MSG_TYPE_PEER:
		{
            sipTransMsg_t* pPeer = pMsg;
		    sipTransaction_t* pTrans = pPeer->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

        	if(pPeer->sipMsgType != SIP_MSG_REQUEST)
        	{
            	logError("received unexpected response message from peer in NISStateProcessing, request message is expected.");
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;
			}		

            //resend the response
			sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }

			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
        {
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
            pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*)pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }

            if(pTrans->resp.rspCode >= 200 && pTrans->resp.rspCode <= 699)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
        }
		case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");

            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			break;
		}
		case SIP_TRANS_MSG_TYPE_TIMEOUT:
		default:
			logError("received unexpected mesType (%d) in sipTransNISStateProceeding, ignore.", msgType);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
			break;	
	}

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransNISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pMsg)
    {
        logError("null pointer, pTrans.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    debug("msgType=%d", msgType);
    switch (msgType)
    {
        case SIP_TRANS_MSG_TYPE_TU:
		{
            sipTransMsg_t* pTU = pMsg;
            if(pTU->sipMsgType != SIP_MSG_RESPONSE)
            {
                logError("received unexpected request message from TU in NISStateCompleted, response message is expected.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            sipTransaction_t* pTrans = pTU->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
			pTrans->pTUId = pTU->pSenderId;
			pTrans->appType = pTU->appType;

			logInfo("received a response from TU in NISStateCompleted, ignore.");
			break;
		}
        case SIP_TRANS_MSG_TYPE_PEER:
		{
            sipTransMsg_t* pPeer = pMsg;
            sipTransaction_t* pTrans = pPeer->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(pPeer->sipMsgType != SIP_MSG_REQUEST)
            {
                logError("received unexpected response message from peer in NISStateCompleted, request message is expected.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            //resend the response
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }
            else if(tpStatus == TRANSPORT_STATUS_TCP_CONN)
            {
                goto EXIT;
            }
            break;
		}
        case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
        {
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
            pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*)pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }
            break;
        }
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to tx msg, received SIP_TRANS_MSG_TYPE_TX_FAILED.");

            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            break;
		}
        case SIP_TRANS_MSG_TYPE_TIMEOUT:
		{
            sipTransaction_t* pTrans = pMsg;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			if(pTrans->sipTransNISTimer.timerIdJ != timerId)
			{
            	logError("received a timerId (%d) that is not equal to timerJ (%d), ignore.", timerId, pTrans->sipTransNISTimer.timerIdJ);
            	status = OS_ERROR_INVALID_VALUE;
            	goto EXIT;
			}

			pTrans->sipTransNISTimer.timerIdJ = 0;
			sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            break;
		}
		default:
			logError("received a unexpected msgType (%d), ignore.", msgType);
			break;
    }

EXIT:
	DEBUG_END
    return status;
}


osStatus_e sipTransNISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;
	sipTransState_e curState = pTrans->state;
	pTrans->state = newState;

	debug("curState=%d, newState=%d.", curState, newState);	
	switch (newState)
	{
		case SIP_TRANS_STATE_TRYING:
		{
            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_MESSAGE;
            pSipTUMsg->sipMsgType = SIP_MSG_REQUEST;
			pSipTUMsg->pPeer = &pTrans->tpInfo.peer;
			pSipTUMsg->pLocal = &pTrans->tpInfo.local;
            sipMsgBuf_copy(&pSipTUMsg->sipMsgBuf, &pTrans->req);
debug("to-remove, pSipTUMsg->sipMsgBuf.pSipMsg=%p, pTrans->resp.pSipMsg=%p.", pSipTUMsg->sipMsgBuf.pSipMsg, pTrans->resp.pSipMsg);
            pSipTUMsg->pTransId = pTrans;
			pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;

            sipTU_onMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg);
			osfree(pSipTUMsg);
			break;
		}
		case SIP_TRANS_STATE_PROCEEDING:
			break;
		case SIP_TRANS_STATE_COMPLETED:
			if(pTrans->tpInfo.tpType == SIP_TRANSPORT_TYPE_UDP)
			{
				pTrans->sipTransNISTimer.timerIdJ = sipTransStartTimer(SIP_TIMER_J, pTrans);
			}
			else
			{
				sipTransNISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
			}
			break;
		case SIP_TRANS_STATE_TERMINATED:
			switch(curState)
			{
				case SIP_TRANS_STATE_TRYING:
                case SIP_TRANS_STATE_PROCEEDING:
				{
					//something wrong, notify UT, treat the error as a transport error
		            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
   			        pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
                    pSipTUMsg->errorInfo.isServerTransaction = true;
                    pSipTUMsg->pTransId = pTrans;
		            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            		pSipTUMsg->pTUId = pTrans->pTUId;

					sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg);
					osfree(pSipTUMsg);
					break;
				}
				case SIP_TRANS_STATE_COMPLETED:
					if(msgType == SIP_TRANS_MSG_TYPE_TX_FAILED)
					{
                        //transport error
	                    sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
	                    pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
	                    pSipTUMsg->errorInfo.isServerTransaction = true;
    	                pSipTUMsg->pTransId = pTrans;
			            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
        	            pSipTUMsg->pTUId = pTrans->pTUId;

	                    sipTU_onMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg);
						osfree(pSipTUMsg);
					}
					break;
				default:
					logError("unexpected NIS state for NIS (%d).", pTrans->state);
					status = OS_ERROR_INVALID_VALUE;
					break;
			}

			//clean up the transaction.
            if(pTrans->sipTransNISTimer.timerIdJ != 0)
            {
                osStopTimer(pTrans->sipTransNISTimer.timerIdJ);
                pTrans->sipTransNISTimer.timerIdJ = 0;
            }

			osHash_deleteNode(pTrans->pTransHashLE, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
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

