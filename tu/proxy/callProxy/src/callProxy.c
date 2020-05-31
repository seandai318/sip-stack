#include "osHash.h"
#include "osTimer.h"

#include "sipConfig.h"
#include "sipHdrVia.h"
#include "sipHeaderMisc.h"
#include "sipHdrMisc.h"

#include "transportIntf.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTransIntf.h"
#include "sipRegistrar.h"
#include "proxyMgr.h"
//#include "callProxyMgr.h"
#include "proxyConfig.h"
#include "proxyHelper.h"
#include "callProxy.h"
#include "callProxyMisc.h"



static osStatus_e callProxy_onSipMsg(sipTUMsg_t* pSipTUMsg,  sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE);
static osStatus_e callProxy_onSipTransError(sipTUMsg_t* pSipTUMsg);
static void callProxy_onTimeout(uint64_t timerId, void* data);
static void callProxyInfo_cleanup(void* pData);

osStatus_e callProxyEnterState(sipCallProxyState_e newState, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateNone_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw);
static osStatus_e callProxyStateInitInvite_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInitError_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInit200Rcvd_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInitAck_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateBye_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInitInvite_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInit200Rcvd_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateInitAck_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateBye_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo);


//can not declare callHash as per thread, unless callProxy_init() is called within a thread, which is not for now
static osHash_t* callHash;



void callProxy_init()
{
	callHash = proxy_getHash();
}
 

osStatus_e callProxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;

	switch (msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			status = callProxy_onSipMsg(pSipTUMsg, pReqDecodedRaw, pHashLE);
			break;
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
		default:
			status =  callProxy_onSipTransError(pSipTUMsg);
			break;
	}

	DEBUG_END
	return status;
}


//for now there is no timeout event for callProxy.
//as a proxy, does not take care whether ACK is received after 200 OK, nor resend 200 OK.
void callProxy_onTimeout(uint64_t timerId, void* data)
{
	callProxyInfo_t* pCallInfo = data;

	if(!pCallInfo)
	{
		logError("timerId(0x%x) times out, but data is null.", timerId);
		return;
	}

	if(timerId == pCallInfo->timerIdC)
	{
		pCallInfo->timerIdC = 0;

		if(pCallInfo->state != SIP_CALLPROXY_STATE_INIT_INVITE)
		{
			logError("received timerIdC timeout in unexpected state(%d), timerId=0x%x.", pCallInfo->state, timerId);
		}

		//notify transaction to cleanup
		void* pUasId = NULL;
		void* pUacId = NULL;
		sipProxy_getPairPrimaryTrId(&pCallInfo->proxyTransInfo, &pUasId, &pUacId);

        sipTransMsg_t sipTransMsg;
		if(pUasId)
		{
    		sipTransMsg.pTransId = pUasId;

		    sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU_FORCE_TERM_TRANS, &sipTransMsg, 0);
		}

		if(pUacId)
        {
            sipTransMsg.pTransId = pUacId;

            sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU_FORCE_TERM_TRANS, &sipTransMsg, 0);
        }
	
        callProxyEnterState(SIP_CALLPROXY_STATE_NONE, pCallInfo);
	}
	else if(timerId == pCallInfo->timerIdWaitAck)
	{
		pCallInfo->timerIdWaitAck = 0;

		if(pCallInfo->state != SIP_CALLPROXY_STATE_INIT_200_RECEIVED)
		{
			logError("received timerIdWaitAck timeout in unexpected state(%d), timerId=0x%x.", pCallInfo->state, timerId);
		}
		
		//simply close the proxy
    	callProxyEnterState(SIP_CALLPROXY_STATE_NONE, pCallInfo);
	}
	else
	{
		logError("received unexpected timeout(0x%x).", timerId);
	}
}	


static osStatus_e callProxy_onSipMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		return OS_ERROR_NULL_POINTER;
	}

	callProxyInfo_t* pCallInfo = pSipTUMsg->pTUId ? ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo : NULL;
	if(!pCallInfo)
	{
		if(pHashLE)
		{
			pCallInfo = ((proxyInfo_t*)((osHashData_t*)pHashLE->data)->pData)->pCallInfo;
		}
	}

	if(pCallInfo)
	{
		switch (pCallInfo->state)
		{
			case SIP_CALLPROXY_STATE_INIT_ERROR:
				return callProxyStateInitError_onMsg(pSipTUMsg, pReqDecodedRaw, pCallInfo);
				break;
			case SIP_CALLPROXY_STATE_INIT_INVITE:
				return callProxyStateInitInvite_onMsg(pSipTUMsg, pReqDecodedRaw, pCallInfo);
				break;
    		case SIP_CALLPROXY_STATE_INIT_200_RECEIVED:
				return callProxyStateInit200Rcvd_onMsg(pSipTUMsg, pReqDecodedRaw, pCallInfo);
				break;
    		case SIP_CALLPROXY_STATE_INIT_ACK_RECEIVED:
				return callProxyStateInitAck_onMsg(pSipTUMsg, pReqDecodedRaw, pCallInfo);
				break;
    		case SIP_CALLPROXY_STATE_BYE:
                return callProxyStateBye_onMsg(pSipTUMsg, pReqDecodedRaw, pCallInfo);
                break;
 			default:
				logError("received a sip message in unexpected state(%d).", pCallInfo->state);
				break;
		}
	}
	else
	{
		return callProxyStateNone_onMsg(pSipTUMsg, pReqDecodedRaw);
	}

	return status;
}


static osStatus_e callProxy_onSipTransError(sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipTUMsg)
    {
        logError("null pointer, pSipTUMsg.");
        return OS_ERROR_NULL_POINTER;
    }

    callProxyInfo_t* pCallInfo = ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo;
    if(!pCallInfo)
    {
		logError("pCallInfo is null.");
		return OS_ERROR_INVALID_VALUE;
	}

    switch (pCallInfo->state)
    {
        case SIP_CALLPROXY_STATE_INIT_INVITE:
            return callProxyStateInitInvite_onTrError(pSipTUMsg, pCallInfo);
            break;
        case SIP_CALLPROXY_STATE_INIT_200_RECEIVED:
            return callProxyStateInit200Rcvd_onTrError(pSipTUMsg, pCallInfo);
            break;
        case SIP_CALLPROXY_STATE_INIT_ACK_RECEIVED:
            return callProxyStateInitAck_onTrError(pSipTUMsg, pCallInfo);
            break;
        case SIP_CALLPROXY_STATE_BYE:
            return callProxyStateBye_onTrError(pSipTUMsg, pCallInfo);
            break;
        default:
            logError("received sip message in unexpected state(%d).", pCallInfo->state);
            break;
    }

	return status;
}


osStatus_e callProxyStateNone_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    callProxyInfo_t* pCallInfo = NULL;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_ACK)
	{
		logInfo("received ACK in SIP_CALLPROXY_STATE_INIT_NONE state, do nothing.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pSipTUMsg->pSipMsgBuf->reqCode != SIP_METHOD_INVITE)
	{
		logError("receives unexpected sip message type (%d).", pSipTUMsg->pSipMsgBuf->reqCode);
		sipProxy_uasResponse(SIP_RESPONSE_503, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//assemble the session info
    pCallInfo = oszalloc(sizeof(callProxyInfo_t), callProxyInfo_cleanup);
	
    osPointerLen_t callId;
    status = sipHdrCallId_getValue(pReqDecodedRaw, &callId);
    osDPL_dup(&pCallInfo->callId, &callId);

    sipHdrCSeq_getValue(pReqDecodedRaw, &pCallInfo->seqNum, NULL);

    pCallInfo->state = SIP_CALLPROXY_STATE_NONE;
    //for now, the proxy does not initiate BYE. when reg is gone, simply drop all sessions.  Otherwise, has to store from, to seq, route set etc.

	//add the session to the hash
    osHashData_t* pHashData = oszalloc(sizeof(osHashData_t), NULL);
    if(!pHashData)
    {
        logError("fails to allocate pHashData.");
		//the ACK for this error response would not be able to reach the proper session as the hash for the callId has not been created.  the outcome is that the transaction may send error response multiple times. to avoid it, we will have to associate callId+seqNum with transactionId in the transaction layer. since this must be an extreme rare situation, we just live with it.
        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    proxyInfo_t* pProxyInfo = oszalloc(sizeof(proxyInfo_t), NULL);
    pProxyInfo->proxyOnMsg = callProxy_onSipTUMsg;
    pProxyInfo->pCallInfo = pCallInfo;
	pCallInfo->pProxyInfo = pProxyInfo;
    pHashData->pData = pProxyInfo;
    pHashData->hashKeyType = OSHASHKEY_INT;
    pHashData->hashKeyInt = osHash_getKeyPL(&callId, true);
logError("to-remvoe, just to check the creation of a address, pProxyInfo=%p, pCallInfo=%p.", pProxyInfo, pCallInfo);
    pCallInfo->pCallHashLE = osHash_add(callHash, pHashData);
    logInfo("callId(%r) is added into callProxyHash, key=0x%x, pCallHashLE=%p, pCallInfo=%p.", &callId, pHashData->hashKeyInt, pCallInfo->pCallHashLE, pCallInfo);

    callProxy_addTrInfo(&pCallInfo->proxyTransInfo, SIP_METHOD_INVITE, pCallInfo->seqNum, pSipTUMsg->pTransId, NULL, true);

	//starts to process routing
    sipUri_t* pTargetUri = NULL;
	sipUri_t tempTargetUri={};	//for !isMultiRoute && use configued next hop case
    osPointerLen_t calledUri;
    status = sipTU_asGetUser(pReqDecodedRaw, &calledUri, false, false);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipTU_asGetCalledUser.");
        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //for now, we only support lr route
    //check the number of route values,
    //  if >= 1, check if the top route belongs to this proxy
    //     if no, error
    //     if yes, remove the top route.  check if there is remaining route
    //        if yes, do nothing more
    //        if no, check if the proxy has registrar, or has next hop configured
    //           if yes, put the called user contact or next hop address in the req uri
    //           if no, reject 404
    //  if == 0, check if the proxy has registrar, or has next hop configured
    //     if yes, put the called user contact or next hop address in the req uri
    //     if no, use the req line host/port as the peer

    //to-do, for now, we do not check if the top route is this proxy, just assume it is, and remove it
    bool isMultiRoute = sipMsg_isHdrMultiValue(SIP_HDR_ROUTE, pReqDecodedRaw, false, NULL);

    transportIpPort_t nextHop;
    if(!isMultiRoute)
    {
        bool isNextHopDone = false;

        //if there is no configured next hop, check if there is registrar, and find the target address from the registrar
        //for this regard, the proxy is treated as a terminating AS
        if(proxyConfig_hasRegistrar())
        {
            status = sipTU_asGetUser(pReqDecodedRaw, &calledUri, false, false);
            if(status != OS_STATUS_OK)
            {
                logError("fails to sipTU_asGetCalledUser.");
		        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

		        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

			tuRegState_e calledRegState;
            pTargetUri = masReg_getUserRegInfo(&calledUri, &calledRegState);
            if(!pTargetUri || calledRegState != MAS_REGSTATE_REGISTERED)
            {
                logInfo("called user (%r) is not registered or null pTargetUri, reject with 404.", &calledUri);
		        sipProxy_uasResponse(SIP_RESPONSE_404, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

		        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
				status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            nextHop.ip = pTargetUri->hostport.host;
            nextHop.port = pTargetUri->hostport.portValue;
            isNextHopDone = true;
        }
        //first check if there is next hop configured
        else if(proxyConfig_getNextHop(&nextHop))
        {
			tempTargetUri.sipUriType = URI_TYPE_SIP;
			tempTargetUri.hostport.host = nextHop.ip;
			tempTargetUri.hostport.portValue = nextHop.port;
			pTargetUri = &tempTargetUri;
            isNextHopDone = true;
        }

        //for initial req but no next hop, use req line
        if(!isNextHopDone)
        {
            sipFirstline_t firstLine;
            status = sipParser_firstLine(pSipTUMsg->pSipMsgBuf->pSipMsg, &firstLine, true);
            nextHop.ip = firstLine.u.sipReqLine.sipUri.hostport.host;
            nextHop.port = firstLine.u.sipReqLine.sipUri.hostport.portValue;
            logInfo("there is no route left, no configured next hop and this proxy does not support registrar, route based on req URI (%r:%d).", &nextHop.ip, nextHop.port);
        }
	}

#if 0
    pCallInfo = oszalloc(sizeof(callProxyInfo_t), callProxyInfo_cleanup);

    osPointerLen_t callId;
	status = sipHdrCallId_getValue(pReqDecodedRaw, &callId);
    osDPL_dup(&pCallInfo->callId, &callId);

	sipHdrCSeq_getValue(pReqDecodedRaw, &pCallInfo->seqNum, NULL);
#endif
	osDPL_dup((osDPointerLen_t*)&pCallInfo->cancelNextHop.ip, &nextHop.ip);
	pCallInfo->cancelNextHop.port = nextHop.port;

#if 0
    pCallInfo->state = SIP_CALLPROXY_STATE_INIT_INVITE;
    //for now, the proxy does not initiate BYE. when reg is gone, simply drop all sessions.  Otherwise, has to store from, to seq, route set etc.
#endif

	if(proxyConfig_hasRegistrar())
	{
        pCallInfo->regId = masReg_addAppInfo(&calledUri, pCallInfo);
        if(!pCallInfo->regId)
        {
	        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

	        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
			status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
	}

#if 0
    //now add callInfo to callHash
    osHashData_t* pHashData = oszalloc(sizeof(osHashData_t), NULL);
    if(!pHashData)
    {
        logError("fails to allocate pHashData.");
        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    proxyInfo_t* pProxyInfo = oszalloc(sizeof(proxyInfo_t), NULL);
    pProxyInfo->proxyOnMsg = callProxy_onSipTUMsg;
    pProxyInfo->pCallInfo = pCallInfo;
    pHashData->pData = pProxyInfo;
    pHashData->hashKeyType = OSHASHKEY_INT;
    pHashData->hashKeyInt = osHash_getKeyPL(&callId, true);
logError("to-remvoe, just to check the creation of a address.");
    pCallInfo->pCallHashLE = osHash_add(callHash, pHashData);
    logInfo("callId(%r) is added into callProxyHash, key=0x%x, pCallHashLE=%p", &callId, pHashData->hashKeyInt, pCallInfo->pCallHashLE);
#endif

	void* pTransId = NULL;
	status = sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, pTargetUri, true, &nextHop, false, pCallInfo->pProxyInfo, &pTransId);
	if(status != OS_STATUS_OK || !pTransId)
	{
		logError("fails to forward sip request, status=%d, pTransId=%p.", status, pTransId);
        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	status = sipProxy_updatePairUacTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, pTransId, true);

    pCallInfo->timerIdC = osStartTimer(SIP_TIMER_C, callProxy_onTimeout, pCallInfo);

	callProxyEnterState(SIP_CALLPROXY_STATE_INIT_INVITE, pCallInfo);

    goto EXIT;

EXIT:
	if(status != OS_STATUS_OK && pCallInfo && pCallInfo->state == SIP_CALLPROXY_STATE_NONE)
	{
		osfree(pCallInfo);
	}

	osfree(pReqDecodedRaw);
	DEBUG_END;
	return status;
}


osStatus_e callProxyStateInitInvite_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;

    uint32_t seqNum;
    osPointerLen_t method;
    status = sipHdrCSeq_getValue(pReqDecodedRaw, &seqNum, &method);
    if(status != OS_STATUS_OK)
    {
        logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");

		if(pSipTUMsg->sipMsgType == SIP_MSG_REQUEST)
		{
        	sipProxy_uasResponse(SIP_RESPONSE_400, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
		}
        goto EXIT;
    }

    switch(pSipTUMsg->sipMsgType)
    {
        case SIP_MSG_REQUEST:
		{
			debug("received a sip request, reqCode=%d.", pSipTUMsg->pSipMsgBuf->reqCode);
			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_INVITE || pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_ACK || pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_BYE)
			{
				logError("received unexpected request(%d), this usually shall not happen, ignore.", pSipTUMsg->pSipMsgBuf->reqCode);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
			
			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_CANCEL)
			{
				//check if seqNum matches, if yes, send back 200 OK response right away
				if(pCallInfo->seqNum == seqNum)
				{
			        sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
		            sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, NULL, false, &pCallInfo->cancelNextHop, true, pCallInfo->pProxyInfo, NULL);
				}
				else
				{
                    sipProxy_uasResponse(SIP_RESPONSE_400, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				}
				goto EXIT;
			}

			//for other requests, just pass.
			void* pUacTransId = NULL;
			status = sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, NULL, true, NULL, false, pCallInfo->pProxyInfo, &pUacTransId);
			if(status != OS_STATUS_OK)
	        {
                sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				status = OS_ERROR_SYSTEM_FAILURE;
				
            }

            callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->pSipMsgBuf->reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, false);

		 	break;
		}
        case SIP_MSG_RESPONSE:
		{
			debug("received a sip response, rspCode=%d.", pSipTUMsg->pSipMsgBuf->rspCode);

			//check the seq num, and method, if seq and method match, if 1xx, restart timerC, and forward the response, if 2xx, change state, and stop timer C.  if >300, stop timerC, close the session. if not match, simply forward the response, stay in the same state.  Do not expect to receive response for CANCEL as transaction layer will drop it.
			sipCallProxyState_e newState = pCallInfo->state;
			if(pCallInfo->seqNum == seqNum && osPL_strcmp(&method, "INVITE")==0)
			{
				if(pSipTUMsg->pSipMsgBuf->rspCode > SIP_RESPONSE_100 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_200)
				{
					pCallInfo->timerIdC = osRestartTimer(pCallInfo->timerIdC);
				}
				else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_300)
				{
					osStopTimer(pCallInfo->timerIdC);
					pCallInfo->timerIdWaitAck = osStartTimer(SIP_TIMER_WAIT_ACK, callProxy_onTimeout, pCallInfo);
					newState = SIP_CALLPROXY_STATE_INIT_200_RECEIVED;
				}
				else
				{
					osStopTimer(pCallInfo->timerIdC);
					pCallInfo->timerIdC = 0;
					newState = SIP_CALLPROXY_STATE_INIT_ERROR;
				}

				void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, true, false);
				if(!pTransId)
				{
					callProxyEnterState(SIP_CALLPROXY_STATE_NONE, pCallInfo);
					goto EXIT;
				}

				status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo);
				if(newState != pCallInfo->state)
				{
					callProxyEnterState(newState, pCallInfo);
				}
			}
			else
			{
				if(osPL_strcmp(&method, "Cancel") == 0)
				{
					goto EXIT;
				}

				bool isRemove = false;
				if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200)
				{
					isRemove = true;
				}
				void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, false, isRemove);
				if(pTransId)
				{
					status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo);
				}
			}
	
            break;
		}
	}

EXIT:
    osfree(pReqDecodedRaw);

	DEBUG_END
	return status;
}
	

static osStatus_e callProxyStateInitError_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    uint32_t seqNum;
    osPointerLen_t method;
    sipHdrCSeq_getValue(pReqDecodedRaw, &seqNum, &method);

    switch(pSipTUMsg->sipMsgType)
    {
        case SIP_MSG_REQUEST:
        {
            //drop CANCEL
            if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_CANCEL && seqNum == pCallInfo->seqNum)
            {
                sipProxy_uasResponse(SIP_RESPONSE_404, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
                goto EXIT;
            }
            else if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_ACK && seqNum == pCallInfo->seqNum)
            {
				sipTransMsg_t ackMsg2Tr;
				sipTransInfo_t sipTransInfo;
				sipTransInfo.isRequest = true;
				sipTransInfo.transId.reqCode = SIP_METHOD_ACK;

				ackMsg2Tr.sipMsgType = SIP_TRANS_MSG_CONTENT_ACK;
				ackMsg2Tr.isTpDirect = false;
				ackMsg2Tr.request.sipTrMsgBuf.sipMsgBuf = *pSipTUMsg->pSipMsgBuf;
				ackMsg2Tr.request.pTransInfo = &sipTransInfo;
				ackMsg2Tr.pTransId = sipProxy_getPairPrimaryUasTrId(&pCallInfo->proxyTransInfo);

				status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &ackMsg2Tr, 0);
			}
			break;
		}
		default:
			logInfo("received a sip response(%d) in SIP_CALLPROXY_STATE_INIT_ERROR , ignore.", pSipTUMsg->pSipMsgBuf->rspCode);	
			break;
	}

EXIT:
	DEBUG_END
	return status;
}

static osStatus_e callProxyStateInitInvite_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo)
{
	osStatus_e status = OS_STATUS_OK;
	return status;
}
	
	
osStatus_e callProxyStateInit200Rcvd_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

	uint32_t seqNum;
    osPointerLen_t method;
    sipHdrCSeq_getValue(pReqDecodedRaw, &seqNum, &method);

    switch(pSipTUMsg->sipMsgType)
    {
        case SIP_MSG_REQUEST:
		{
			//drop CANCEL
			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_CANCEL && seqNum == pCallInfo->seqNum)
            {
                sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				goto EXIT;
            }
			else if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_INVITE)
            {
                //not allow re-invite at this state
                sipProxy_uasResponse(SIP_RESPONSE_403, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
                goto EXIT;
            }

			void* pUacTransId;
			bool isTpDirect = false;
			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_ACK)
			{
				isTpDirect = true;
			}
            status = sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, NULL, true, NULL, isTpDirect, pCallInfo->pProxyInfo, &pUacTransId);
            if(status != OS_STATUS_OK)
            {
                sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				goto EXIT;
            }

			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_BYE)
			{
				osStopTimer(pCallInfo->timerIdWaitAck);
				pCallInfo->timerIdWaitAck = 0;

				callProxyEnterState(SIP_CALLPROXY_STATE_BYE, pCallInfo);
			}
			else if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_ACK)
			{
                //need to free the buf allocated for the ACK message after forwarding the request.
                osfree(pSipTUMsg->pSipMsgBuf->pSipMsg);

				if(seqNum == pCallInfo->seqNum)
				{
                	osStopTimer(pCallInfo->timerIdWaitAck);
                	pCallInfo->timerIdWaitAck = 0;

					sipProxy_removePairTrInfo(&pCallInfo->proxyTransInfo, NULL, true);
					callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ACK_RECEIVED, pCallInfo);
				}
			}
			else
			{
				callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->pSipMsgBuf->reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, false);
			}		
	
			break;
		}
		case SIP_MSG_RESPONSE:
		{
			bool isPrimary = false;
            bool isRemove = false;
            if(pCallInfo->seqNum == seqNum && osPL_strcmp(&method, "INVITE")==0)
			{
				//continue forward 200 OK, for the primary session, otherwise, drop
				if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_300)
				{
					isPrimary = true;
				}
				else
				{
					goto EXIT;
				}
			}
            else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200)
            {
                isRemove = true;
            }

            void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, isPrimary, isRemove);
            if(pTransId)
            {
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo);
            }
			break;
		}
	}

EXIT:
    osfree(pReqDecodedRaw);

	DEBUG_END
	return status;	
}


static osStatus_e callProxyStateInit200Rcvd_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo)
{
	osStatus_e status = OS_STATUS_OK;
	return status;
}


//for now we do not implement re-invite
osStatus_e callProxyStateInitAck_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    uint32_t seqNum;
    sipHdrCSeq_getValue(pReqDecodedRaw, &seqNum, NULL);

    switch(pSipTUMsg->sipMsgType)
    {
        case SIP_MSG_REQUEST:
		{
			void* pUacTransId;
            status = sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, NULL, true, NULL, false, pCallInfo->pProxyInfo, &pUacTransId);
            callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->pSipMsgBuf->reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, false);

            if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_BYE)
            {
                callProxyEnterState(SIP_CALLPROXY_STATE_BYE, pCallInfo);
            }
			break;
		}
        case SIP_MSG_RESPONSE:
        {
            bool isRemove = false;
            if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200)
            {
                isRemove = true;
            }
            void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, false, isRemove);
            if(pTransId)
            {
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo);
            }
            break;
        }
    }

EXIT:
    osfree(pReqDecodedRaw);

	DEBUG_END
    return status;
}
	

static osStatus_e callProxyStateInitAck_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo)
{
	osStatus_e status = OS_STATUS_OK;
	return status;
}


osStatus_e callProxyStateBye_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, callProxyInfo_t* pCallInfo)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    uint32_t seqNum;
	osPointerLen_t method;
    sipHdrCSeq_getValue(pReqDecodedRaw, &seqNum, &method);

    switch(pSipTUMsg->sipMsgType)
    {
        case SIP_MSG_REQUEST:
			if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_CANCEL)
			{
                sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
			}
			else if(pSipTUMsg->pSipMsgBuf->reqCode != SIP_METHOD_ACK)
			{
                sipProxy_uasResponse(SIP_RESPONSE_403, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
			}
			break;
		case SIP_MSG_RESPONSE:
		{
			//set isRemove=false.  the record will be removed when the session is cleaned
            void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, false, false);
            if(pTransId)
            {
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo);
            }

			if(osPL_strcmp(&method, "BYE") == 0 && (pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_300))
			{
                callProxyEnterState(SIP_CALLPROXY_STATE_NONE, pCallInfo);
			}
			break;
		}
	}

EXIT:
    osfree(pReqDecodedRaw);

	DEBUG_END
	return status;
}


static osStatus_e callProxyStateBye_onTrError(sipTUMsg_t* pSipTUMsg, callProxyInfo_t* pCallInfo)
{
	osStatus_e status = OS_STATUS_OK;
	return status;
}
						
	
osStatus_e callProxyEnterState(sipCallProxyState_e newState, callProxyInfo_t* pCallInfo)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pCallInfo)
	{
		logError("null pointer, pCallInfo.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	logInfo("pCallInfo(%p) switches from state(%d) to new state(%d).", pCallInfo, pCallInfo->state, newState);

	switch(newState)
	{
		case SIP_CALLPROXY_STATE_NONE:
			osfree(pCallInfo);
			break;
		case SIP_CALLPROXY_STATE_INIT_ERROR:
			if(pCallInfo->timerIdC !=0)
			{
				osStopTimer(pCallInfo->timerIdC);
			}

			if(pCallInfo->timerIdWaitAck ==0)
			{
            	pCallInfo->timerIdWaitAck = osStartTimer(SIP_TIMER_WAIT_ACK, callProxy_onTimeout, pCallInfo);
			}
			pCallInfo->state = newState;
			break;
		default:
			pCallInfo->state = newState;
			break;
	}

EXIT:
	return status;
}


static void callProxyInfo_cleanup(void* pData)
{
	if(!pData)
	{
		logError("null pointer, pData");
	}

	callProxyInfo_t* pCallInfo = pData;

	if(pCallInfo->timerIdC)
	{
        osStopTimer(pCallInfo->timerIdC);
        pCallInfo->timerIdC = 0;
    }

    if(pCallInfo->timerIdWaitAck)
    {
        osStopTimer(pCallInfo->timerIdWaitAck);
        pCallInfo->timerIdWaitAck = 0;
    }

    //remove from registrar if regId != 0
    if(pCallInfo->regId)
    {
        masReg_deleteAppInfo(pCallInfo->regId, pCallInfo);
    }

    //remove from hash
    if(pCallInfo->pCallHashLE)
    {
        logInfo("delete hash element, key=%u", ((osHashData_t*)pCallInfo->pCallHashLE->data)->hashKeyInt);
        osHash_deleteNode(pCallInfo->pCallHashLE);

		//remove memory allocated for proxyInfo_t
		osfree(((osHashData_t*)pCallInfo->pCallHashLE->data)->pData);
		osfree(pCallInfo->pCallHashLE->data);	
    	osfree(pCallInfo->pCallHashLE);
    	pCallInfo->pCallHashLE = NULL;
	}

	osDPL_dealloc(&pCallInfo->callId);
	osDPL_dealloc((osDPointerLen_t*)&pCallInfo->cancelNextHop.ip);
	osListPlus_delete(&pCallInfo->proxyTransInfo);
}


#if 0
static osStatus_e callProxy_onSipRequest(sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    callProxyInfo_t* pCallInfo = NULL;

    if(!sipTU_isProxyCallReq(pSipTUMsg->pSipMsgBuf->reqCode))
    {
        logError("receives unexpected sip message type (%d).", pSipTUMsg->pSipMsgBuf->reqCode);
        rspCode = SIP_RESPONSE_503;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
        logError("fails to sipDecodeMsgRawHdr. The received sip message may not be correctly encoded.");
        rspCode = SIP_RESPONSE_400;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    osPointerLen_t callId;
    sipParserHdr_str(pReqDecodedRaw->msgHdrList[SIP_HDR_CALL_ID]->pRawHdr, pReqDecodedRaw->msgHdrList[SIP_HDR_CALL_ID]->pRawHdr->end, &callId);

    uint32_t key = osHash_getKeyPL(&callId, true);
    osListElement_t* pHashLE = osHash_lookupByKey(callProxyHash, &key, OSHASHKEY_INT);

#if 0
	bool isOrig;
	status = sipTU_asGetSescase(pReqDecodedRaw, &isOrig);

    //do not accept terminating SIP MESSAGE
    if(!isOrig || status != OS_STATUS_OK)
    {
        rspCode = SIP_RESPONSE_503;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //get the user's sip URI
	osPointerLen_t callerUri;
	status = sipTU_asGetUser(pReqDecodedRaw, &callerUri, true, isOrig);
	if(status != OS_STATUS_OK)
	{
   		logError("fails to sipTU_asGetUser.");
   		rspCode = SIP_RESPONSE_500;
   		status = OS_ERROR_INVALID_VALUE;
   		goto EXIT;
	}
#endif

	sipUri_t* pTargetUri = NULL;
	osPointerLen_t calledUri;
	status = sipTU_asGetUser(pReqDecodedRaw, &calledUri, false, isOrig);
	if(status != OS_STATUS_OK)
    {
        logError("fails to sipTU_asGetCalledUser.");
       	rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//for now, we only support lr route
	//check the number of route values, 
	//  if >= 1, check if the top route belongs to this proxy
	//     if no, error
	//     if yes, remove the top route.  check if there is remaining route
	//        if yes, do nothing more
	//        if no, check if the proxy has registrar, or has next hop configured 
	//           if yes, put the called user contact or next hop address in the req uri
	//           if no, reject 404
	//  if == 0, check if the proxy has registrar, or has next hop configured
    //     if yes, put the called user contact or next hop address in the req uri
    //     if no, use the req line host/port as the peer

    //to-do, for now, we do not check if the top route is this proxy, just assume it is, and remove it
	bool isMultiRoute = sipMsg_isHdrMultiValue(SIP_HDR_ROUTE, pReqDecodedRaw, false, NULL);

	transportIpPort_t nextHop;
	if(!isMultiRoute)
	{
		bool isNextHopDone = false;

		//if the request is for the creation of a session
		if(!pHashLE)
		{
            //if there is no configured next hop, check if there is registrar, and find the target address from the registrar
            //for this regard, the proxy is treated as a terminating AS
			if(proxyConfig_hasRegistrar())
			{
			   	status = sipTU_asGetUser(pReqDecodedRaw, &calledUri, false, false);
    			if(status != OS_STATUS_OK)
    			{
       				logError("fails to sipTU_asGetCalledUser.");
       				rspCode = SIP_RESPONSE_500;
        			status = OS_ERROR_INVALID_VALUE;
        			goto EXIT;
    			}

			   	pTargetUri = masReg_getUserRegInfo(&calledUri, &calledRegState);
    			if(!pTargetUri || calledRegState != MAS_REGSTATE_REGISTERED)
    			{
        			logInfo("called user (%r) is not registered or null pTargetUri, reject with 404.", &calledUri);
        			rspCode = SIP_RESPONSE_404;
        			goto EXIT;
    			}

				nextHop.ip = pTargetUri->hostport.host;
    			nextHop.port = pTargetUri->hostport.portValue;
                isNextHopDone = true;
			}
			//first check if there is next hop configured
			else if(proxyConfig_getNextHop(&nextHop))
			{
				isNextHopDone = true;
            }

		}

		//for subsequent req or for initial req but no next hop, use req line
		if(!isNextHopDone)
		{
    		sipFirstline_t firstLine;
    		status = sipParser_firstLine(pSipTUMsg->pSipMsgBuf->pSipMsg, &firstLine, true);
			nextHop.ip = firstLine.u.sipReqLine.sipUri.hostport.host;
			nextHop.port = firstLine.u.sipReqLine.sipUri.hostport.portValue;
            logInfo("there is no route left, no configured next hop and this proxy does not support registrar, route based on req URI (%r:%d).", &nextHop.ip, nextHop.port);
		}
	}
		
	size_t topViaProtocolPos = 0;
	sipTransInfo_t sipTransInfo;
	sipTransInfo.transId.viaId.host = pSipTUMsg->pPeer->ip;
	sipTransInfo.transId.viaId.port = pSipTUMsg->pPeer->port;

	//prepare message forwarding
	sipHdrRawValueId_t delList = {SIP_HDR_ROUTE, true};
	uint8_t delNum = pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE] ? 1 : 0;
	char* localIP;
	int localPort;
    sipConfig_getHostStr(&localIP, &localPort);
	char* ipPort = oszalloc_r(21);
	int len = sprintf(ipPort, "%s:%d", &localIP, localPort);
	osPointerLen_t rr = {ipPort, len};
	sipHdrRawValueStr_t addList = {SIP_HDR_Record_ROUTE, &rr};

    //forward the SIP INVITE, add top via, remove top Route, reduce the max-forarded by 1.  The viaId shall be filled with the real peer IP/port
	osMBuf_t* pReq = sipTU_b2bBuildRequest(pReqDecodedRaw, true, &delList, delNum, &addList, 1, &sipTransInfo.transId.viaId, pTargetUri, &topViaProtocolPos);
	osfree(ipPort);
	if(!pReq)
	{
		logError("fails to create proxy request.");
		rspCode = SIP_RESPONSE_503;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
	}

	if(!pHashLE)
	{
		callProxyInfo_t* pCallInfo = oszalloc(sizeof(callProxyInfo_t), callProxyInfo_cleanup);
		osDPL_dup(&pCallInfo->callId, &callId);
        pCallInfo->state = SIP_CALLPROXY_STATE_INIT_INVITE;
		//for now, the proxy does not initiate BYE. when reg is gone, simply drop all sessions.  Otherwise, has to store from, to seq, route set etc.

		pCallInfo->regId = masReg_addAppInfo(&calledUri, pCallInfo);
		if(!pCallInfo->regId)
		{
			osMBuf_dealloc(pReq);
			pCallInfo = osfree(pCallInfo);
			rspCode = SIP_RESPONSE_500;
			status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
    	}

		//now add callInfo to callHash
        osHashData_t* pHashData = oszalloc(sizeof(osHashData_t), NULL);
        if(!pHashData)
        {
            logError("fails to allocate pHashData.");
            osMBuf_dealloc(pReq);
            pCallInfo = osfree(pCallInfo);
            rspCode = SIP_RESPONSE_500;
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

        pHashData->hashKeyType = OSHASHKEY_INT;
        pHashData->hashKeyInt = key;
        pHashData->pData = pCallInfo;
logError("to-remvoe, just to check the creation of a address.");
        pCallInfo->pCallHashLE = osHash_add(callProxyHash, pHashData);
        logInfo("callId(%r) is add into callProxyHash, key=0x%x, pCallHashLE=%p", &callId, key, pCallInfo->pCallHashLE);
	}
	else
	{
		pCallInfo = ((osHashData_t*)pHashLE->data)->pData;
	}

    logInfo("SIP Request Message=\n%M", pReq);

	sipTransMsg_t sipTransMsg;
	sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
    sipTransInfo.isRequest = true;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = pSipTUMsg->pSipMsgBuf->reqCode;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;
	sipTransInfo.transId.reqCode = pSipTUMsg->pSipMsgBuf->reqCode;
	sipTransMsg.request.pTransInfo = &sipTransInfo;
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.ip = nextHop.ip;
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.port = nextHop.port;
    sipConfig_getHost(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.request.sipTrMsgBuf.tpInfo.local.port);
	sipTransMsg.request.sipTrMsgBuf.tpInfo.viaProtocolPos = topViaProtocolPos;
	sipTransMsg.pTransId = NULL;
	sipTransMsg.pSenderId = pCallInfo;
					
    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

	//proxy does not need to keep pReq.  If other laters need it, it is expected they will add ref to it
    osfree(pReq);

	if(pCallInfo->state = SIP_CALLPROXY_STATE_INIT_INVITE)
	{
		pCallInfo->timerIdC = osStartTimer(SIP_TIMER_C, callProxy_onTimeout, pCallInfo); 
	}

	goto EXIT;

BUILD_RESPONSE:
	logInfo("create the response for the request from caller, rspCode=%d", rspCode);

	//to send the response to the caller side
	if(rspCode != SIP_RESPONSE_INVALID)
	{
        sipHostport_t peer;
        peer.host = pSipTUMsg->pPeer->ip;
        peer.portValue = pSipTUMsg->pPeer->port;

	    sipHdrDecoded_t viaHdr={};
    	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to decode the top via hdr in sipDecodeHdr.");
        	goto EXIT;
    	}

        osMBuf_t* pSipResp = NULL;
        sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
        int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
        pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
        status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
        status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
        status = sipTU_msgBuildEnd(pSipResp, false);

        if(pSipResp)
        {
            logInfo("Response Message=\n%M", pSipResp);

            sipTransMsg_t sipTransMsg = {};

            //fill the peer transport info
            sipHdrViaDecoded_t* pTopVia = ((sipHdrMultiVia_t*)(viaHdr.decodedHdr))->pVia;
            sipHostport_t peerHostPort;
            sipTransport_e peerTpProtocol;
            sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);

            sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
//            sipTransMsg.response.sipTrMsgBuf.tpInfo.tcpFd = pSipTUMsg->tcpFd;
            sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.ip = peerHostPort.host;
            sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.port = peerHostPort.portValue;
            sipConfig_getHost(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.response.sipTrMsgBuf.tpInfo.local.port);
            sipTransMsg.response.sipTrMsgBuf.tpInfo.viaProtocolPos = 0;

            //fill the other info
            sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
            sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
            sipTransMsg.pTransId = pSipTUMsg->pTransId;
            sipTransMsg.response.rspCode = rspCode;
            sipTransMsg.pSenderId = pCallInfo;

            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

			//proxy does not need to keep pSipResp.  If other laters need it, it is expected they will add ref to it
			osfree(pSipResp);
		}
        else
        {
            logError("fails to sipTU_buildResponse.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        }

		osfree(viaHdr.decodedHdr);
		osfree(pReqDecodedRaw);
	}

EXIT:
//this test log here is dangerous, as calledContact may not be dcleared for some case, remove it as soon as the debug is done
//logError("to-remove ASAP, calledContact, host=%r, port=%d", &pTargetUri->hostport.host, pTargetUri->hostport.portValue);

	DEBUG_END
	return status;
}


static osStatus_e callProxy_onSipResponse(sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto CLEAN_UP;
	}

	callProxyInfo_t* pCallInfo = pSipTUMsg->pTUId;
	if(!pCallInfo)
	{
		logError("received rspCode(%d), but pMasInfo=NULL", pSipTUMsg->pSipMsgBuf->rspCode);
		goto CLEAN_UP;
	}		

	if(pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_200)
	{
		debug("received rspCode(%d) for masInfo (%p), ignore.", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);
		goto EXIT;
	}
	else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p)", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);

		//delete the stored SMS if successfullyr eceived by the called
		if(pMasInfo->smsType == MAS_SMS_TYPE_DB)
		{
			status = masDbDeleteSms(pMasInfo->uacData.dbSmsId);
		}
		goto CLEAN_UP;
	}
	else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p).", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);

		//for MAS_SMS_TYPE_DB, the msg has been stored and timer has been updated
		if(pMasInfo->smsType != MAS_SMS_TYPE_DB)
		{
            status = masDbStoreSms(-1, (osPointerLen_t*)&pMasInfo->uacData.user, (osPointerLen_t*)&pMasInfo->uacData.caller, (osPointerLen_t*)&pMasInfo->uacData.sms);
		}
	}

CLEAN_UP:
    masReg_deleteAppInfo(pMasInfo->regId, pMasInfo->pSrcTransId);
    osfree(pMasInfo);

EXIT:
	return status;
}


uint64_t masSMSStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masSMS_onTimeout, pData);
}


static osStatus_e masSMS_onSipTransError(sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(!pSipTUMsg->pTUId)
	{
        logError("null pointer, pSipTUMsg->pTUId.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	masReg_deleteAppInfo(((masInfo_t*)pSipTUMsg->pTUId)->regId, ((masInfo_t*)pSipTUMsg->pTUId)->pSrcTransId);

EXIT:
	DEBUG_END
	return status;
}
#endif
