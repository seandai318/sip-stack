/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file callProxy.c
 ********************************************************/


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



static osStatus_e callProxy_onSipMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteModCtl, proxyInfo_t** ppProxyInfo, void* pProxyMgrInfo);

static osStatus_e callProxy_onSipTransError(sipTUMsg_t* pSipTUMsg);
static void callProxy_onTimeout(uint64_t timerId, void* data);
static void callProxyInfo_cleanup(void* pData);

osStatus_e callProxyEnterState(sipCallProxyState_e newState, callProxyInfo_t* pCallInfo);
static osStatus_e callProxyStateNone_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteModCtl, void* pProxyMgrInfo, proxyInfo_t** ppProxyInfo);
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
static proxyStatusNtfyCB_h fProxyStatusNtfyCB; //callback function to notify proxyMgr the proxy status change
//to-do, proxyReg2RegistrarCB_h and proxyDelFromRegistrarCB_h can be removed to be part of proxyStatusNtfyCB_h
static proxyReg2RegistrarCB_h fProxyRegCB;		//callback to register to registrar.  This CB maybe NULL since some application (like CSCF) already handle it by fProxyStatusNtfyCB, there is no need for this CB
static proxyDelFromRegistrarCB_h fProxyDelCB;	//callback to delete a proxy from registrar

void callProxy_init(proxyStatusNtfyCB_h proxyStatusNtfy, proxyReg2RegistrarCB_h proxyReg2Registrar, proxyDelFromRegistrarCB_h proxyDelFromRegistrar)
{
	if(!proxyStatusNtfy)
	{
		logError("null pointer, proxyStatusNtfy=%p.", proxyStatusNtfy);
		return;
	}

	fProxyStatusNtfyCB = proxyStatusNtfy;
	fProxyRegCB = proxyReg2Registrar;
	fProxyDelCB = proxyDelFromRegistrar;
}
	 

//pProxyMgrInfo: proxyMgr info that associated with the aprticular proxy.  for example, for scscf, it is scscfSessInfo_t, for standalone proxyMgr, it is osListElement_t* pHashLE.  That is used to pass back to the proxyMgr for function fProxyCreation and fProxyStatusNtfy.  This parameter must be no NULL in the initial request
osStatus_e callProxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteModCtl, proxyInfo_t** ppProxyInfo, void* pProxyMgrInfo)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;

	switch (msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			status = callProxy_onSipMsg(pSipTUMsg, pReqDecodedRaw, pRouteModCtl, ppProxyInfo, pProxyMgrInfo);
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


//static osStatus_e callProxy_onSipMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE)
static osStatus_e callProxy_onSipMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteModCtl, proxyInfo_t** ppProxyInfo, void* pProxyMgrInfo)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg=%p.", pSipTUMsg);
		return OS_ERROR_NULL_POINTER;
	}

	callProxyInfo_t* pCallInfo = pSipTUMsg->pTUId ? ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo : NULL;
	if(!pCallInfo && *ppProxyInfo)
	{
		pCallInfo = (*ppProxyInfo)->pCallInfo;
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
		return callProxyStateNone_onMsg(pSipTUMsg, pReqDecodedRaw, pRouteModCtl, pProxyMgrInfo, ppProxyInfo);
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


static osStatus_e callProxyStateNone_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteModCtl, void* pProxyMgrInfo, proxyInfo_t** ppProxyInfo)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    callProxyInfo_t* pCallInfo = NULL;

	if(!pSipTUMsg || !ppProxyInfo)
	{
		logError("null pointer, pSipTUMsg=%p, ppProxyInfo=%p.", pSipTUMsg, ppProxyInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(*ppProxyInfo)
	{
		logError("pProxyInfo(%p) already exists for a initial request.", *ppProxyInfo);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_ACK)
	{
		logInfo("received ACK in SIP_CALLPROXY_STATE_INIT_NONE state, do nothing.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pSipTUMsg->sipMsgBuf.reqCode != SIP_METHOD_INVITE)
	{
		logError("receives unexpected sip message type (%d).", pSipTUMsg->sipMsgBuf.reqCode);
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

	//request proxyMgr to create a proxy and add pCallInfo to the proxy.  Reason to let proxyMgr to create a proxy is that there may have different criteria to create a session, like callId, ODI(in scscf), etc., only proxyMgr knows the criteria
    *ppProxyInfo = oszalloc(sizeof(proxyInfo_t), proxyInfo_cleanup);
	if(!(*ppProxyInfo))
	{
		logError("fails to allocate a proxyInfo for request (callId=%r).", &callId);
        //the ACK for this error response would not be able to reach the proper session as the hash for the callId has not been created.  the outcome is that the transaction may send error response multiple times. to avoid it, we will have to associate callId+seqNum with transactionId in the transaction layer. since this must be an extreme rare situation, we just live with it.
        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	pCallInfo->pProxyMgrInfo = pProxyMgrInfo;
    pCallInfo->pProxyInfo = *ppProxyInfo;
    (*ppProxyInfo)->pCallInfo = pCallInfo;
    (*ppProxyInfo)->proxyOnMsg = callProxy_onSipTUMsg;
    logInfo("proxy for callId(%r) is created(%p), pProxyMgrInfo=%p, pCallInfo=%p.", &callId, *ppProxyInfo, pProxyMgrInfo, pCallInfo);

#if 0
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
#endif

    callProxy_addTrInfo(&pCallInfo->proxyTransInfo, SIP_METHOD_INVITE, pCallInfo->seqNum, pSipTUMsg->pTransId, NULL, pCallInfo->pProxyInfo, true);

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

    sipTuAddr_t nextHop={};
	if(pRouteModCtl && pRouteModCtl->pNextHop)
	{
		nextHop = *pRouteModCtl->pNextHop;
	}
	else
	{
	    //if there is pRouteModCtl and pRouteModCtl->pNextHop != NULL, use it for next route.
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

    	//to-do, for now, we do not check if the top route is this proxy, just assume it is, and remove it (to-be-done)
	    bool isMultiRoute = sipMsg_isHdrMultiValue(SIP_HDR_ROUTE, pReqDecodedRaw, false, NULL);
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

            	nextHop.ipPort.ip = pTargetUri->hostport.host;
            	nextHop.ipPort.port = pTargetUri->hostport.portValue;
            	isNextHopDone = true;
        	}
        	//first check if there is next hop configured
        	else if(proxyConfig_getNextHop(&nextHop.ipPort))
        	{
				tempTargetUri.sipUriType = URI_TYPE_SIP;
				tempTargetUri.hostport.host = nextHop.ipPort.ip;
				tempTargetUri.hostport.portValue = nextHop.ipPort.port;
				pTargetUri = &tempTargetUri;
            	isNextHopDone = true;
        	}

        	//for initial req but no next hop, use req line
        	if(!isNextHopDone)
        	{
            	sipFirstline_t firstLine;
            	status = sipParser_firstLine(pSipTUMsg->sipMsgBuf.pSipMsg, &firstLine, true);
            	nextHop.ipPort.ip = firstLine.u.sipReqLine.sipUri.hostport.host;
            	nextHop.ipPort.port = firstLine.u.sipReqLine.sipUri.hostport.portValue;
            	logInfo("there is no route left, no configured next hop and this proxy does not support registrar, route based on req URI (%r:%d).", &nextHop.ipPort.ip, nextHop.ipPort.port);
        	}
		}
	}

	if(nextHop.isSockAddr)
	{
		pCallInfo->cancelNextHop = nextHop;
	}
	else
	{
		pCallInfo->cancelNextHop.isSockAddr = true;
        osIpPort_t ipPort = {{nextHop.ipPort.ip, false, false}, nextHop.ipPort.port};
        osConvertPLton(&ipPort, true, &pCallInfo->cancelNextHop.sockAddr);
	}

#if 0
    pCallInfo->state = SIP_CALLPROXY_STATE_INIT_INVITE;
    //for now, the proxy does not initiate BYE. when reg is gone, simply drop all sessions.  Otherwise, has to store from, to seq, route set etc.
#endif

//	if(proxyConfig_hasRegistrar())
	if(fProxyRegCB)
	{
        pCallInfo->regId = fProxyRegCB(&calledUri, pCallInfo);
        if(!pCallInfo->regId)
        {
	        sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);

	        callProxyEnterState(SIP_CALLPROXY_STATE_INIT_ERROR, pCallInfo);
			status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
	}

	void* pTransId = NULL;
	sipProxy_msgModInfo_t msgModInfo = {true, true};
	sipProxy_msgModInfo_t* pMsgModInfo = pRouteModCtl ? &pRouteModCtl->msgModInfo : &msgModInfo;

#if 1	//to be replaxced to change pTargetUri type to sipTuUri_t, to-do
	sipTuUri_t targetUri;
	if(pTargetUri)
	{
		targetUri.isRaw = false;
		targetUri.sipUri = *pTargetUri;
	}
#endif
debug("to-remove, before sipProxy_forwardReq");
	status = sipProxy_forwardReq(SIPTU_APP_TYPE_PROXY, pSipTUMsg, pReqDecodedRaw, pTargetUri ? &targetUri : NULL, pMsgModInfo, &nextHop, false, pCallInfo->pProxyInfo, &pTransId);
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
	if(status != OS_STATUS_OK)
	{
		if(pCallInfo && pCallInfo->state == SIP_CALLPROXY_STATE_NONE)
		{
			osfree(pCallInfo);
		}

		*ppProxyInfo = osfree(*ppProxyInfo);
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
			debug("received a sip request, reqCode=%d.", pSipTUMsg->sipMsgBuf.reqCode);
			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE || pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_ACK || pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_BYE)
			{
				logError("received unexpected request(%d), this usually shall not happen, ignore.", pSipTUMsg->sipMsgBuf.reqCode);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
			
			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_CANCEL)
			{
				//check if seqNum matches, if yes, send back 200 OK response right away
				if(pCallInfo->seqNum == seqNum)
				{
			        sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
					sipProxy_msgModInfo_t msgModInfo = {true, false};
		            sipProxy_forwardReq(SIPTU_APP_TYPE_PROXY, pSipTUMsg, pReqDecodedRaw, NULL, &msgModInfo, &pCallInfo->cancelNextHop, true, pCallInfo->pProxyInfo, NULL);
				}
				else
				{
                    sipProxy_uasResponse(SIP_RESPONSE_400, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				}
				goto EXIT;
			}

			//for other requests, just pass.
			void* pUacTransId = NULL;
			sipProxy_msgModInfo_t msgModInfo = {true, true};
			status = sipProxy_forwardReq(SIPTU_APP_TYPE_PROXY, pSipTUMsg, pReqDecodedRaw, NULL, &msgModInfo, NULL, false, pCallInfo->pProxyInfo, &pUacTransId);
			if(status != OS_STATUS_OK)
	        {
                sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				status = OS_ERROR_SYSTEM_FAILURE;
				
            }

            callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->sipMsgBuf.reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, pCallInfo->pProxyInfo, false);

		 	break;
		}
        case SIP_MSG_RESPONSE:
		{
			debug("received a sip response, rspCode=%d, method=%r, pCallInfo->seqNum=%d, seqNum=%d.", pSipTUMsg->sipMsgBuf.rspCode, &method, pCallInfo->seqNum, seqNum);

			//check the seq num, and method, if seq and method match, if 1xx, restart timerC, and forward the response, if 2xx, change state, and stop timer C.  if >300, stop timerC, close the session. if not match, simply forward the response, stay in the same state.  Do not expect to receive response for CANCEL as transaction layer will drop it.
			sipCallProxyState_e newState = pCallInfo->state;
			if(pCallInfo->seqNum == seqNum && osPL_strcmp(&method, "INVITE")==0)
			{
				if(pSipTUMsg->sipMsgBuf.rspCode > SIP_RESPONSE_100 && pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_200)
				{
					pCallInfo->timerIdC = osRestartTimer(pCallInfo->timerIdC);
				}
				else if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200 && pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_300)
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

				status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);
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
				if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200)
				{
					isRemove = true;
				}
				void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, false, isRemove);
				if(pTransId)
				{
					status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);
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
            if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_CANCEL && seqNum == pCallInfo->seqNum)
            {
                sipProxy_uasResponse(SIP_RESPONSE_404, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
                goto EXIT;
            }
            else if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_ACK && seqNum == pCallInfo->seqNum)
            {
				sipTransMsg_t ackMsg2Tr;
				sipTransInfo_t sipTransInfo;
				sipTransInfo.isRequest = true;
				sipTransInfo.transId.reqCode = SIP_METHOD_ACK;

				ackMsg2Tr.sipMsgType = SIP_TRANS_MSG_CONTENT_ACK;
				ackMsg2Tr.isTpDirect = false;
				ackMsg2Tr.request.sipTrMsgBuf.sipMsgBuf = pSipTUMsg->sipMsgBuf;
				ackMsg2Tr.request.pTransInfo = &sipTransInfo;
				ackMsg2Tr.pTransId = sipProxy_getPairPrimaryUasTrId(&pCallInfo->proxyTransInfo);

				status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &ackMsg2Tr, 0);
			}
			break;
		}
		default:
			logInfo("received a sip response(%d) in SIP_CALLPROXY_STATE_INIT_ERROR , ignore.", pSipTUMsg->sipMsgBuf.rspCode);	
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
			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_CANCEL && seqNum == pCallInfo->seqNum)
            {
                sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				goto EXIT;
            }
			else if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_INVITE)
            {
                //not allow re-invite at this state
                sipProxy_uasResponse(SIP_RESPONSE_403, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
                goto EXIT;
            }

			void* pUacTransId;
			bool isTpDirect = false;
			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_ACK)
			{
				isTpDirect = true;
			}

		    sipProxy_msgModInfo_t msgModInfo = {true, true};
            status = sipProxy_forwardReq(SIPTU_APP_TYPE_PROXY, pSipTUMsg, pReqDecodedRaw, NULL, &msgModInfo, NULL, isTpDirect, pCallInfo->pProxyInfo, &pUacTransId);
            if(status != OS_STATUS_OK)
            {
                sipProxy_uasResponse(SIP_RESPONSE_500, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
				goto EXIT;
            }

			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_BYE)
			{
				osStopTimer(pCallInfo->timerIdWaitAck);
				pCallInfo->timerIdWaitAck = 0;

				callProxyEnterState(SIP_CALLPROXY_STATE_BYE, pCallInfo);
			}
			else if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_ACK)
			{
                //need to free the buf allocated for the ACK message after forwarding the request.
                osfree(pSipTUMsg->sipMsgBuf.pSipMsg);

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
				callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->sipMsgBuf.reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, pCallInfo->pProxyInfo, false);
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
				if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200 && pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_300)
				{
					isPrimary = true;
				}
				else
				{
					goto EXIT;
				}
			}
            else if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200)
            {
                isRemove = true;
            }

            void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, isPrimary, isRemove);
            if(pTransId)
            {
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);
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
		    sipProxy_msgModInfo_t msgModInfo = {true, true};
            status = sipProxy_forwardReq(SIPTU_APP_TYPE_PROXY, pSipTUMsg, pReqDecodedRaw, NULL, &msgModInfo, NULL, false, pCallInfo->pProxyInfo, &pUacTransId);
            callProxy_addTrInfo(&pCallInfo->proxyTransInfo, pSipTUMsg->sipMsgBuf.reqCode, seqNum, pSipTUMsg->pTransId, pUacTransId, pCallInfo->pProxyInfo, false);

            if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_BYE)
            {
                callProxyEnterState(SIP_CALLPROXY_STATE_BYE, pCallInfo);
            }
			break;
		}
        case SIP_MSG_RESPONSE:
        {
            bool isRemove = false;
            if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200)
            {
                isRemove = true;
            }
            void* pTransId = sipProxy_getPairUasTrId(&pCallInfo->proxyTransInfo, pSipTUMsg->pTransId, false, isRemove);
            if(pTransId)
            {
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);
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
			if(pSipTUMsg->sipMsgBuf.reqCode == SIP_METHOD_CANCEL)
			{
                sipProxy_uasResponse(SIP_RESPONSE_200, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
			}
			else if(pSipTUMsg->sipMsgBuf.reqCode != SIP_METHOD_ACK)
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
                status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);
            }

			if(osPL_strcmp(&method, "BYE") == 0 && (pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200 && pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_300))
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
    if(pCallInfo->regId && fProxyDelCB)
    {
		fProxyDelCB(pCallInfo->regId, pCallInfo);
//        masReg_deleteAppInfo(pCallInfo->regId, pCallInfo);
    }

    //remove from hash
    if(pCallInfo->pProxyMgrInfo)
    {
		fProxyStatusNtfyCB(pCallInfo->pProxyMgrInfo, pCallInfo->pProxyInfo, SIP_PROXY_STATUS_DELETE);
        logInfo("remove proxy(%p) from proxyMgr(%p) for callId(%r)", pCallInfo->pProxyInfo, pCallInfo->pProxyMgrInfo, &pCallInfo->callId);
//		pCallInfo->pCallHashLE = NULL;
#if 0
		//remove memory allocated for proxyInfo_t
        osHash_deleteNode(pCallInfo->pCallHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
        pCallInfo->pCallHashLE = NULL;
#endif
#if 0
		//remove memory allocated for proxyInfo_t
		osfree(((osHashData_t*)pCallInfo->pCallHashLE->data)->pData);
		osfree(pCallInfo->pCallHashLE->data);	
    	osfree(pCallInfo->pCallHashLE);
    	pCallInfo->pCallHashLE = NULL;
#endif
	}

	osDPL_dealloc(&pCallInfo->callId);
	osListPlus_delete(&pCallInfo->proxyTransInfo);
}
