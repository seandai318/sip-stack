/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTransInviteServer.c
 ********************************************************/


#include "osMisc.h"
#include "osTimer.h"
#include "osHash.h"
#include "osPreMemory.h"

#include "sipMsgRequest.h"
#include "sipTransIntf.h"
#include "sipTransMgr.h"
#include "sipTUIntf.h"
#include "sipTransportIntf.h"
#include "sipConfig.h"
#include "sipCodecUtil.h"


static osStatus_e sipTransISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans);
static osStatus_e sipTransISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateConfirmed_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);
static osStatus_e sipTransIS_build100Trying(sipMsgBuf_t* pSipBufReq, sipMsgBuf_t* pSipMsgBuf);


osStatus_e sipTransInviteServer_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
	DEBUG_END
    return status;
}


osStatus_e sipTransISStateNone_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
        case SIP_TRANS_MSG_TYPE_PEER:
		{
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

//    		pTrans->tpInfo.pTrId = pTrans;
//			pTrans->tpInfo = ((sipTransMsg_t*)pMsg)->request.sipTrMsgBuf.tpInfo;

    		//build a 100 TRYING and send
    		status = sipTransIS_build100Trying(&pTrans->req, &pTrans->resp);

    		sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
			if(tpStatus == TRANSPORT_STATUS_UDP || tpStatus == TRANSPORT_STATUS_TCP_OK)
			{
				pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;
    			sipTransISEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
			}
    		else if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
    		{
        		sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
    		}

			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
		{
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
			pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
			pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*) pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_OK)
            {
				pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
                sipTransISEnterState(SIP_TRANS_STATE_PROCEEDING, msgType, pTrans);
            }
            else if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
            {
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
            break;
		}
		default:
        	logError("receive unexpected msgType (%d) in ISStateNone.", msgType);
        	status = OS_ERROR_INVALID_VALUE;
        	break;
	}	

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransISStateProceeding_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
		case SIP_TRANS_MSG_TYPE_PEER:
		{
			sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;

            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_RMT_NOT_ACCESSIBLE)
            {
                //notify TU
                sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
                pSipTUMsg->pTransId = pTrans;
	            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
                pSipTUMsg->pTUId = pTrans->pTUId;
                pSipTUMsg->sipTuMsgType = tpStatus == TRANSPORT_STATUS_FAIL ? SIP_TU_MSG_TYPE_TRANSPORT_ERROR : SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE;
                pSipTUMsg->errorInfo.isServerTransaction = true;

                sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
				osfree(pSipTUMsg);

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }

            pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;
	
			break;
		}
		case SIP_TRANS_MSG_TYPE_TU:
		{
			if(((sipTransMsg_t*)pMsg)->sipMsgType != SIP_MSG_RESPONSE)
			{
				logError("received unexpected sipMsgType (%d), ignore.", ((sipTransMsg_t*)pMsg)->sipMsgType);
				goto EXIT;
			}

            sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;

            //the peer address from TU is the address in the top via, may be different from the real address used by the peer.  if the request message was received via TCP, the real peer has been saved when transaction was created, only reset when the message was received via UDP.
            if(pTrans->tpInfo.tcpFd < 0)
            {
				pTrans->tpInfo.peer = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.tpInfo.peer;
            }

#if 0	//use struct sockaddr_in
			//only copy if the local address has not been stored.		
			if(pTrans->tpInfo.local.ip.l == 0)
			{	
            	osDPL_dup((osDPointerLen_t*)&pTrans->tpInfo.local.ip, &((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.tpInfo.local.ip);
            	pTrans->tpInfo.local.port = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.tpInfo.local.port;
			}
#else
			pTrans->tpInfo.local = ((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.tpInfo.local;
#endif
			pTrans->pTUId = ((sipTransMsg_t*)pMsg)->pSenderId;
            pTrans->appType = ((sipTransMsg_t*)pMsg)->appType;
            sipResponse_e rspCode = ((sipTransMsg_t*)pMsg)->response.rspCode;
			osfree(pTrans->resp.pSipMsg);
			pTrans->resp.pSipMsg = osmemref(((sipTransMsg_t*)pMsg)->response.sipTrMsgBuf.sipMsgBuf.pSipMsg);

            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_RMT_NOT_ACCESSIBLE)
            {
                //notify TU
                sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
                pSipTUMsg->pTransId = pTrans;
                pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
                pSipTUMsg->pTUId = pTrans->pTUId;

                pSipTUMsg->sipTuMsgType = tpStatus == TRANSPORT_STATUS_FAIL ? SIP_TU_MSG_TYPE_TRANSPORT_ERROR : SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE;
                pSipTUMsg->errorInfo.isServerTransaction = true;

                sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
				osfree(pSipTUMsg);

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }
            else if(tpStatus == TRANSPORT_STATUS_TCP_CONN)
            {
				pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
                goto EXIT;
            }

            pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;

            if(rspCode >= 100 && rspCode < 200)
            {
				//stay in the same state
            }
            else if(rspCode >= 200 && rspCode < 300)
            {
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
			else
			{
                sipTransISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TX_TCP_READY:
        {
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*) pMsg)->pTransId;
            if(!pTrans)
            {
                logError("null pointer, pTrans, for msgType (%d).", msgType);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            pTrans->tpInfo.tpType = SIP_TRANSPORT_TYPE_TCP;
            pTrans->tpInfo.tcpFd = ((sipTransportStatusMsg_t*)pMsg)->tcpFd;
            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL || tpStatus == TRANSPORT_STATUS_RMT_NOT_ACCESSIBLE)
            {
				//notify TU
                sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
    	        pSipTUMsg->pTransId = pMsg;
                pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
        	    pSipTUMsg->pTUId = pTrans->pTUId;

                pSipTUMsg->sipTuMsgType = tpStatus == TRANSPORT_STATUS_FAIL ? SIP_TU_MSG_TYPE_TRANSPORT_ERROR : SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE;
                pSipTUMsg->errorInfo.isServerTransaction = true;

                sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
				osfree(pSipTUMsg);

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
                goto EXIT;
            }

            pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;

            if(pTrans->resp.rspCode >= 100 && pTrans->resp.rspCode <=199)
            {
				//stay in the same state
            }
            else if(pTrans->resp.rspCode >= 200 && pTrans->resp.rspCode < 300)
            {
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            }
            else
            {
                sipTransISEnterState(SIP_TRANS_STATE_COMPLETED, msgType, pTrans);
            }
            break;
        }		
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
			logInfo("fails to transmit message, received SIP_TRANS_MSG_TYPE_TX_FAILED.");
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;

            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;

            pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
            pSipTUMsg->errorInfo.isServerTransaction = true;

            sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
			osfree(pSipTUMsg);

			sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			break;
		}
        case SIP_TRANS_MSG_TYPE_TU_FORCE_TERM_TRANS:
            sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, ((sipTransMsg_t*)pMsg)->pTransId);
            goto EXIT;
            break;
        default:
            logError("received unexpected msgType (%d), ignore.", msgType);
            break;
    }

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipTransISStateCompleted_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
        case SIP_TRANS_MSG_TYPE_PEER:
		{
            sipTransaction_t* pTrans = ((sipTransMsg_t*)pMsg)->pTransId;
			if(((sipTransMsg_t*)pMsg)->sipMsgType == SIP_MSG_REQUEST)
			{
	            sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
	            if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
    	        {
        	        sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
            	    goto EXIT;
            	}

	            pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;

			}
			else if(((sipTransMsg_t*)pMsg)->sipMsgType == SIP_MSG_ACK)
			{
				sipTransISEnterState(SIP_TRANS_STATE_CONFIRMED, msgType, pTrans);
			}
			else
			{
				logError("received unexpected sipMsgType: SIP_MSG_RESPONSE in IS state COMPLETED, ignore.");
			}
			break;
		}
		case SIP_TRANS_MSG_TYPE_TIMEOUT:
		{
			sipTransaction_t* pTrans = pMsg;
            if(pTrans->sipTransISTimer.timerIdG == timerId)
            {
                pTrans->sipTransISTimer.timerIdG = 0;
                //osfree(pTrans);
                logInfo("timer G expires, retransmit the request.");

                sipTransportStatus_e tpStatus = sipTransport_send(pTrans, &pTrans->tpInfo, pTrans->resp.pSipMsg);
                if(tpStatus == TRANSPORT_STATUS_TCP_FAIL || tpStatus == TRANSPORT_STATUS_FAIL)
                {
                    logError("fails to send request.");
                    sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TX_FAILED, pTrans);
                    goto EXIT;
                }

	            pTrans->tpInfo.tpType = (tpStatus == TRANSPORT_STATUS_UDP) ? SIP_TRANSPORT_TYPE_UDP :SIP_TRANSPORT_TYPE_TCP;

				if(tpStatus == TRANSPORT_STATUS_UDP)
                {
                    pTrans->timerAEGValue = osMinInt(pTrans->timerAEGValue*2, SIP_TIMER_T2);
                    pTrans->sipTransISTimer.timerIdG = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                }
            }
            else if(pTrans->sipTransISTimer.timerIdH == timerId)
            {
                pTrans->sipTransISTimer.timerIdH = 0;
                // osfree(pTrans);
                logInfo("timer H expires, terminate the transaction.");

                if(pTrans->sipTransISTimer.timerIdG !=0)
                {
                    osStopTimer(pTrans->sipTransISTimer.timerIdG);
                    pTrans->sipTransISTimer.timerIdG = 0;
                //    osfree(pTrans);
                }

                sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
    	        pSipTUMsg->pTransId = pTrans;
                pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
        	    pSipTUMsg->pTUId = pTrans->pTUId;

	            pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
    	        pSipTUMsg->errorInfo.isServerTransaction = true;

        	    sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
				osfree(pSipTUMsg);

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
			}
			break;
		}
        case SIP_TRANS_MSG_TYPE_TX_FAILED:
		{
            logInfo("fails to transmit message, received SIP_TRANS_MSG_TYPE_TX_FAILED.");
            sipTransaction_t* pTrans = ((sipTransportStatusMsg_t*)pMsg)->pTransId;

            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;
            pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_TRANSACTION_ERROR;
            pSipTUMsg->errorInfo.isServerTransaction = true;

            sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
			osfree(pSipTUMsg);

			sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, msgType, pTrans);
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



osStatus_e sipTransISStateConfirmed_onMsg(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId)
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
        	if(pTrans->sipTransISTimer.timerIdI == timerId)
            {
                pTrans->sipTransISTimer.timerIdI = 0;
				osfree(pTrans);
                debug("timer I expires, terminate the transaction.");

                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_TIMEOUT, pTrans);
                goto EXIT;
            }
            else
            {
                logError("receive an unexpected timerId(0x%x), ignore.", timerId);
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
	DEBUG_END
	return status;
}


osStatus_e sipTransISEnterState(sipTransState_e newState, sipTransMsgType_e msgType, sipTransaction_t* pTrans)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	sipTransState_e prvState = pTrans->state;
	pTrans->state = newState;

	logInfo("switch state from %d to %d.", prvState, newState);

    switch (newState)
    {
        case SIP_TRANS_STATE_PROCEEDING:
		{
            sipTUMsg_t* pSipTUMsg = osmalloc(sizeof(sipTUMsg_t), sipTUMsg_cleanup);
            pSipTUMsg->sipMsgType = SIP_MSG_REQUEST;
            pSipTUMsg->pPeer = &pTrans->tpInfo.peer;
			pSipTUMsg->pLocal = &pTrans->tpInfo.local;
            sipMsgBuf_copy(&pSipTUMsg->sipMsgBuf, &pTrans->req);
            pSipTUMsg->pTransId = pTrans;
            pSipTUMsg->appType = pTrans ? pTrans->appType : SIPTU_APP_TYPE_NONE;
            pSipTUMsg->pTUId = pTrans->pTUId;

			pSipTUMsg->sipTuMsgType = SIP_TU_MSG_TYPE_MESSAGE;
			sipTU_onMsg(pSipTUMsg->sipTuMsgType, pSipTUMsg);
			osfree(pSipTUMsg);

			break;
		}
		case SIP_TRANS_STATE_COMPLETED:	
            pTrans->sipTransISTimer.timerIdH = sipTransStartTimer(SIP_TIMER_H, pTrans);
            if(pTrans->sipTransISTimer.timerIdH == 0)
            {
                logError("fails to start timerH.");
                sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            if(pTrans->tpInfo.tpType == SIP_TRANSPORT_TYPE_UDP)
            {
                pTrans->timerAEGValue = SIP_TIMER_T1;
                pTrans->sipTransISTimer.timerIdG = sipTransStartTimer(pTrans->timerAEGValue, pTrans);
                if(pTrans->sipTransISTimer.timerIdG == 0)
                {
                    logError("fails to start timerG.");
                    sipTransISEnterState(SIP_TRANS_STATE_TERMINATED, SIP_TRANS_MSG_TYPE_INTERNAL_ERROR, pTrans);
                    status = OS_ERROR_INVALID_VALUE;
                    goto EXIT;
                }
            }
            break;
        case SIP_TRANS_STATE_CONFIRMED:
			if(pTrans->sipTransISTimer.timerIdG != 0)
			{
				osStopTimer(pTrans->sipTransISTimer.timerIdG);
				pTrans->sipTransISTimer.timerIdG = 0;
			}
			
            if(pTrans->sipTransISTimer.timerIdH != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdH);
                pTrans->sipTransISTimer.timerIdH = 0;
            }

			if(pTrans->tpInfo.tpType == SIP_TRANSPORT_TYPE_UDP)
			{
            	pTrans->sipTransISTimer.timerIdI = sipTransStartTimer(SIP_TIMER_I, pTrans);
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
				osfree(pTrans);
            }
            if(pTrans->sipTransISTimer.timerIdH != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdH);
                pTrans->sipTransISTimer.timerIdH = 0;
                osfree(pTrans);
            }
            if(pTrans->sipTransISTimer.timerIdI != 0)
            {
                osStopTimer(pTrans->sipTransISTimer.timerIdI);
                pTrans->sipTransISTimer.timerIdI = 0;
                osfree(pTrans);
            }

            osHash_deleteNode(pTrans->pTransHashLE, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
#if 0
            osHashData_t* pHashData = pTrans->pTransHashLE->data;
			osfree(pHashData);
            //osfree((sipTransaction_t*)pHashData->pData);
            osfree(pTrans->pTransHashLE);
#endif
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


static osStatus_e sipTransIS_build100Trying(sipMsgBuf_t* pSipBufReq, sipMsgBuf_t* pSipMsgBuf)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;
	
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = NULL;

	if(!pSipBufReq || !pSipMsgBuf)
	{
		logError("null pointer, pSipBufReq=%p, pSipMsgBuf=%p.", pSipBufReq, pSipMsgBuf);
		goto EXIT;
	}

	pSipMsgBuf->pSipMsg = osMBuf_alloc(SIP_MAX_MSG_SIZE);
	if(!pSipMsgBuf->pSipMsg)
	{
		logError("fails to allocate pSipMsgBuf->pSipMsg.");
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
    status = sipHdrFirstline_respEncode(pSipMsgBuf->pSipMsg, &rspCode, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to encode the response, respCode=%d.", rspCode);
        goto EXIT;
    }

	sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
	for(int i=0; i<arraySize; i++)
	{
		status = sipMsgAddHdr(pSipMsgBuf->pSipMsg, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], NULL, ctrl);
		if(status != OS_STATUS_OK)
	    {
    	    logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
        	goto EXIT;
    	}
	}

    uint32_t contentLen = 0;
    sipHdrAddCtrl_t ctrlCL = {false, false, false, NULL};
    status = sipMsgAddHdr(pSipMsgBuf->pSipMsg, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	logInfo("rspMsg=\n%M", pSipMsgBuf->pSipMsg);
	
EXIT:
	osfree(pReqDecodedRaw);

	DEBUG_END
	return status;
}
	
