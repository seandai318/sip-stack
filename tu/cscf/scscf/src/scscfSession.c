/*************************************************************************************
 * Copyright (C) 2019,2020,2021 Sean Dai
 *
 * @file scscfSession.c
 * This file processes the initial requests only to route the traffic to AS or
 * the final destination.  The subsequent requests will reach this file, but will
 * be directly forwarded to the corresponding proxies.  The responses would not
 * even reach this file, instead, they are directly sent to proxies by TU.
 *
 * This file maintains a hash for each session.  The session contains a list of
 * proxies that the session traverses, including both MO and MT if applicable.
 * When a proxy is established (for INVITE, ACK is received, for other requests,
 * 200 OK is received), or when a proxy is to be removed (for INVITE, 200 OK for
 * BYE or CANCEL, for SUBSCRIBE, when 200 OK for term state is received, for other
 * requests, when 200 OK is received), it will notify this file to update the session
 * state or remove the session from the hash. 
 *************************************************************************************/

#include "osTypes.h"
#include "osHash.h"
#include "osDebug.h"
#include "osPL.h"
#include "osLB.h"
#include "osPrintf.h"

#include "diaMsg.h"
#include "diaCxSar.h"
#include "diaCxLir.h"
#include "diaCxAvp.h"

#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipHdrMisc.h"
#include "sipCodecUtil.h"
#include "sipTUIntf.h"
#include "sipTUMisc.h"
#include "proxyMgr.h"
#include "proxyHelper.h"
#include "sipTU.h"

#include "icscfCxLir.h"
#include "scscfSession.h"
#include "cscfHelper.h"
#include "scscfIfc.h"
#include "scscfIntf.h"



static osStatus_e scscfSess_onSipRsp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pProxyInfo, sipProxy_msgModInfo_t* pModInfo);
static osStatus_e scscfSess_isInitialReq(scscfSessInfo_t* pSessInfo, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool* isInitialReq);
static void scscfSess_onProxyStatus(void* pSessInfo, proxyInfo_t* pProxy, proxyStatus_e proxyStatus);
static proxyInfo_t* scscfSess_getMatchingProxy(scscfSessInfo_t* pSessInfo, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw);

static osStatus_e scscfSess_enterState(scscfSessInfo_t* pSessInfo, scscfSessState_e newSessState);
static osStatus_e scscfSessStateInit_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pCallId, sipHdrGenericNameParam_t* pTopRoute);
static osStatus_e scscfSessStateMoInit_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateSar_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateDnsAs_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateMTInit_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateMO2MTInit_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateDnsEnum_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateLir_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateToLocalSCSCF_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateToBreakout_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateToGeoSCSCF_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateToUe_start(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateEstablished_onMsg(scscfSessInfo_t* pSessInfo);
static osStatus_e scscfSessStateClosing_onMsg(scscfSessInfo_t* pSessInfo);

static void scscfSessStateSar_onSaaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData);
static osStatus_e scscfSessStateDnsAs_fwdRequest(scscfSessInfo_t* pSessInfo, dnsResResponse_t* pRR, bool isCachedDnsRsp);
static void scscfSessStateLir_onLia(diaMsgDecoded_t* pDiaDecoded, void* pAppData);
static void scscfSessState_dnsCallback(dnsResResponse_t* pRR, void* pData);
static void scscfSessStateClosing_onProxyDelete(scscfSessInfo_t* pSessInfo, proxyInfo_t* pProxy);
static osStatus_e scscf_buildReqModInfo(scscfSessInfo_t* pSessInfo, bool isOrig, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pAS, sipTuAddr_t* pScscfAddr, sipTuRR_t* pRRScscf, sipProxy_msgModInfo_t* pMsgModInfo);
static osPointerLen_t* scscfSess_createOdi(scscfSessInfo_t* pSessInfo);


static void scscfSessInfo_cleanup(void* data);
static void scscfSessInfo_workInfoCleanup(scscfSessInfo_t* pSessInfo);
static void scscfSessInfo_workInfoReset(scscfSessInfo_t* pSessInfo, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pCallId);


//right now the creation of hash is called from the main program, the gSessHash can not be per thread.  if want per thread hash, need to call the proxy_init() directly from a thread.
static osHash_t* gSessHash;


osStatus_e scscfSess_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    gSessHash = osHash_create(bucketSize);
    if(!gSessHash)
    {
        logError("fails to create gSessHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	sipTU_attach(SIPTU_APP_TYPE_SCSCF_SESSION, scscfSess_onTUMsg);

	proxy_init(scscfSess_onProxyStatus);

EXIT:
    return status;
}


//this function handles the request only.  The response shall be directly forwarded to the proxy
//if there is error,scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING) is only called in the function that starts the 
//thread based on the error status, the other function simply returns status. 
osStatus_e scscfSess_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;
	sipHdrDecoded_t sipHdrDecoded = {};
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = NULL;
	scscfSessInfo_t* pSessInfo = NULL;

    switch (msgType)
    {
        case SIP_TU_MSG_TYPE_MESSAGE:
		{
			if(!pSipTUMsg->sipMsgBuf.isRequest)
			{
				logError("receives a sip response that is expected to directly be forwarded to a proxy.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			//handle requests only.  For response, they will be directly forwarded to a proxy
		    pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
    		if(pReqDecodedRaw == NULL)
    		{
        		logError("fails to sipDecodeMsgRawHdr. The received sip message may not be correctly encoded.");
				rspCode = SIP_RESPONSE_400;
        		goto EXIT;
    		}

			/* all legs for a call share the same sessionInfo.  The sessionInfo is hashed with hash key being callId and route header's user part.*/
            osPointerLen_t callId;
            status = sipHdrCallId_getValue(pReqDecodedRaw, &callId);
			pSessInfo = osPlHash_getUserData(gSessHash, &callId, true);	
			if(pSessInfo)
			{
                scscfSessInfo_workInfoReset(pSessInfo, pSipTUMsg, pReqDecodedRaw, NULL);

				bool isOdiMatch = false;
				status = scscfSess_isInitialReq(pSessInfo, pReqDecodedRaw, &isOdiMatch);
				if(status != OS_STATUS_OK)
				{
					logError("fails to scscfSess_isInitialReq for callid(%r).", &callId);
					rspCode = SIP_RESPONSE_400;
					goto EXIT;
				}

                //for a initial request
				if(isOdiMatch)
				{
					//odi is used to find the matching proxy, delete the temporary hash entry that used odi
					osHash_deleteNode(pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
					pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = NULL;
					pSessInfo->tempWorkInfo.isInitial = false;	//this request has been forwarded by the SCSCF before

					if(pSessInfo->state != SCSCF_SESS_STATE_DNS_AS)
					{
                        logError("receive a sip request(callId=%r) while the suer's session state(%d) is not SCSCF_SESS_STATE_DNS_AS.", &callId, pSessInfo->state);
						rspCode = SIP_RESPONSE_500;
                        goto EXIT;
					}

					status = scscfSessStateDnsAs_onMsg(pSessInfo);
                    if(status != OS_STATUS_OK)
                    {
                        logError("fails to process request for callId=%r.", &callId, pSessInfo->tempWorkInfo.isMO);
                        goto EXIT;
					}
				}
				else
				{
					//if subsequent request, find the matching proxyInfo, and forward the request to the proxy. be noted that
					//after a message is sent to proxy, proxy will keep track of pSipTUMsg & pReqDecodedRaw, and send back response
					//(error or normal response), scscfSess does not need to initiate error response any more
					proxyInfo_t* pProxyInfo = scscfSess_getMatchingProxy(pSessInfo, pSipTUMsg, pReqDecodedRaw);
					if(!pProxyInfo)
					{
						logError("fail to find proxy for the new request(callId=%r).", &callId);
						rspCode = SIP_RESPONSE_500;
                        goto EXIT;
                    }

					sipProxyAppInfo_t appInfo = {scscfSess_onSipRsp, pSessInfo};
					status = proxy_onSipTUMsgViaApp(msgType, pSipTUMsg, pReqDecodedRaw, NULL, &pProxyInfo, &appInfo); 
					if(status != OS_STATUS_OK)
					{
                        logError("fail to send message via proxy for the new request(callId=%r).", &callId);
						rspCode = SIP_RESPONSE_500;
                        goto EXIT;
                    }
				}
			}
			else
			{
				//do not find session using callId, it is possible AS may changes the callId when the AS is a B2BUA, check using odi
    			sipHdrGenericNameParam_t* pTopRoute = sipDecodeTopRouteValue(pReqDecodedRaw, &sipHdrDecoded, false);
    			if(pTopRoute)
				{
					pSessInfo = osPlHash_getUserData(gSessHash, &pTopRoute->uri.userInfo.sipUser.user, true);
					if(pSessInfo)
					{
						osListElement_t* pCallIdHashElem = osPlHash_addUserData(gSessHash, &callId, true, pSessInfo);

	                    //odi is used to find the matching proxy, delete the temporary hash entry that used odi
    	                osHash_deleteNode(pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
        	            pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = NULL;
						pSessInfo->tempWorkInfo.isInitial = false;	//this request has been forwarded by the CSCF before

						//cleanup the existing workInfo, set new pSipTUMsg & pReqDecodedRaw
						scscfSessInfo_workInfoReset(pSessInfo, pSipTUMsg, pReqDecodedRaw, &callId);
						osList_append(&pSessInfo->callIdHashElemList, pCallIdHashElem);

	                    if(pSessInfo->state != SCSCF_SESS_STATE_DNS_AS)
    	                {
        	                logError("receive a sip request(callId=%r) while the user's session state(%d) is not SCSCF_SESS_STATE_DNS_AS.", &callId, pSessInfo->state);
							rspCode = SIP_RESPONSE_500;
                	        goto EXIT;
                    	}

                    	status = scscfSessStateDnsAs_onMsg(pSessInfo);
					    if(status != OS_STATUS_OK)
    					{
        					logError("fails to process request for callId=%r, isMO=%d.", &callId, pSessInfo->tempWorkInfo.isMO);
        					goto EXIT;
    					}
	
						break;
					}
				}

				//if !pTopRoute, or pTopRoute does not have matching odi, it is a brand-new request
				status = scscfSessStateInit_onMsg(pSipTUMsg, pReqDecodedRaw, &callId, pTopRoute);
			}
			break;
		}
        case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
		default:
			//for error, they shall be directly routed to proxy
			logError("received msgType=%d, they shall be directly routed to proxy.", msgType);
			break;
	}	//switch(msgType)

EXIT:
	osfree(sipHdrDecoded.decodedHdr);

    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
    }

	if(status != OS_STATUS_OK && pSessInfo)
	{
	    scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
	}

	DEBUG_END
	return status;
}


//this is the scscfSess_onTUMsg() for sip response.  instead receiving from sipTU directly, the sip rsp message is received via proxy.  scsxf may add/remove headers in this function by passing out pModInfo
static osStatus_e scscfSess_onSipRsp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pProxyInfo, sipProxy_msgModInfo_t* pModInfo)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg || !pReqDecodedRaw || !pProxyInfo || !pModInfo)
	{
		logError("null pointer, pSipTUMsg=%p, pReqDecodedRaw=%p, pProxyInfo=%p, pModInfo=%p.", pSipTUMsg, pReqDecodedRaw, pProxyInfo, pModInfo);
		return OS_ERROR_NULL_POINTER;
	}

EXIT:
	return status;
}
	

static osStatus_e scscfSessStateInit_onMsg(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pCallId, sipHdrGenericNameParam_t* pTopRoute)
{
	osStatus_e status = OS_STATUS_OK;
	scscfSessInfo_t* pSessInfo = NULL;
	sipResponse_e rspCode = SIP_RESPONSE_200;

	if(!pSipTUMsg || !pReqDecodedRaw || !pCallId || !rspCode)
	{
		logError("null pointer, pSipTUMsg=%p, pReqDecodedRaw=%p, pCallId=%p, rspCode=%p.", pSipTUMsg, pReqDecodedRaw, pCallId, rspCode);
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_NULL_POINTER;
		return status;
	}

    pSessInfo = oszalloc(sizeof(scscfSessInfo_t), scscfSessInfo_cleanup);
	if(!pSessInfo)
	{
		logError("fails to oszalloc for pSessInfo for callid(%r).", pCallId);
		rspCode = SIP_RESPONSE_500;
		goto EXIT;
	}

	osListElement_t* pCallIdHashElem = osPlHash_addUserData(gSessHash, pCallId, true, pSessInfo);
	osList_append(&pSessInfo->callIdHashElemList, pCallIdHashElem);

	//differentiate a orig or term request
    osPointerLen_t sescaseName = {"orig", 4};
    pSessInfo->tempWorkInfo.isMO = sipUri_isParamInOther(&pTopRoute->uri, &sescaseName);
	pSessInfo->state = SCSCF_SESS_STATE_INIT;
	mdebug(LM_CSCF, "state SCSCF_SESS_STATE_INIT, pSessInfo(%p) initiated, isMO=%d", pSessInfo, pSessInfo->tempWorkInfo.isMO);

	pSessInfo->tempWorkInfo.pSipTUMsg = pSipTUMsg;
	pSessInfo->tempWorkInfo.pReqDecodedRaw = pReqDecodedRaw;
	pSessInfo->tempWorkInfo.callId = *pCallId;
	pSessInfo->tempWorkInfo.userNum = SCSCF_MAX_PAI_NUM;
	pSessInfo->tempWorkInfo.isInitial = true;	//this request never be forwarded by the SCSCF before

	status = cscf_getRequestUser(pSipTUMsg, pReqDecodedRaw, pSessInfo->tempWorkInfo.isMO, pSessInfo->tempWorkInfo.users, &pSessInfo->tempWorkInfo.userNum);
	if(status != OS_STATUS_OK)
	{
		logError("fails to cscf_getRequestUser for callId(%r).", pCallId);
		rspCode = SIP_RESPONSE_500;
		goto EXIT;
	}

	if(pSessInfo->tempWorkInfo.userNum < 1)
	{
		status = OS_ERROR_EXT_INVALID_VALUE;
		logError("could not get user from the received sip request, callId=%r, isMO=%d", pCallId, pSessInfo->tempWorkInfo.isMO);
		rspCode = SIP_RESPONSE_400;
		goto EXIT;
	}

	status = scscfSess_enterState(pSessInfo, pSessInfo->tempWorkInfo.isMO ? SCSCF_SESS_STATE_MO_INIT : SCSCF_SESS_STATE_MT_INIT);

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
    }

    if(status != OS_STATUS_OK && pSessInfo)
    {
        scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);

#if 0
		if(pSessInfo)
		{
			osHash_deleteNode(pSessInfo->hashElem, OS_HASH_DEL_NODE_TYPE_ALL);
		}
#endif
    }

    return status;
}


osStatus_e scscfSessStateMoInit_onMsg(scscfSessInfo_t* pSessInfo)
{
	osStatus_e status = OS_STATUS_OK;
	pSessInfo->pRegInfo = scscfReg_getRegInfo(&pSessInfo->tempWorkInfo.users[0], &pSessInfo->userRegState, &pSessInfo->sIfcIdList);	
	sipResponse_e rspCode = SIP_RESPONSE_200;
	if(!pSessInfo->pRegInfo)
	{
        /* for NOT_REGISTERED case, perform SAR to download user profile.  do not check if the user is anchored in local or geo CSCF.  
		   if the user is already anchored in the local or geo CSCF, SAR will fail.
	     */ 
		status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_SAR);
		goto EXIT;
	}

	//there is regInfo for the user, but the state is SCSCF_REG_STATE_NOT_REGISTERED, implies the user is doing initial registration
	if(pSessInfo->userRegState == SCSCF_REG_STATE_NOT_REGISTERED)
	{
		logInfo("the user(%r) is performing initial registration, reject the request.", &pSessInfo->tempWorkInfo.users[0]);
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
	
	//check if any user is barred
	osPointerLen_t* pBarredUser = scscfReg_getAnyBarredUser(pSessInfo->pRegInfo, pSessInfo->tempWorkInfo.users, pSessInfo->tempWorkInfo.userNum);
	if(pBarredUser)
	{
		logInfo("a user(%r) is barred, isMO=%d, total user num=%d", pBarredUser, pSessInfo->tempWorkInfo.isMO, pSessInfo->tempWorkInfo.userNum);
		rspCode = SIP_RESPONSE_403;
		status = OS_ERROR_EXT_INVALID_VALUE;
		goto EXIT;
	}

	logInfo("create a session(%p), isMO=%d, user=%r, callId=%r", pSessInfo, pSessInfo->tempWorkInfo.isMO, &pSessInfo->tempWorkInfo.users[0],  &pSessInfo->tempWorkInfo.callId);

	status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_DNS_AS);
	if(status != OS_STATUS_OK)
	{
		logError("fails to process request for user(%r), callId=%r, isMO=%d.", &pSessInfo->tempWorkInfo.users[0],  &pSessInfo->tempWorkInfo.callId, pSessInfo->tempWorkInfo.isMO);
		goto EXIT;
	}

EXIT:
	if(rspCode != SIP_RESPONSE_200)
	{
        logError("fail to perform init for the new request(callId=%r).", &pSessInfo->tempWorkInfo.callId);
       	sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
	}

	return status;	
}


static osStatus_e scscfSessStateSar_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    diaCxSarInfo_t sarInfo = {DIA_3GPP_CX_UNREGISTERED_USER, DIA_3GPP_CX_USER_DATA_NOT_AVAILABLE, DIA_3GPP_CX_MULTI_REG_IND_NOT_MULTI_REG, NULL};
    osPointerLen_t serverName = {SCSCF_URI_WITH_PORT, strlen(SCSCF_URI_WITH_PORT)};
    //calculate HSSDest
    struct sockaddr_in* pDest = diaConnGetActiveDest(DIA_INTF_TYPE_CX);
    if(!pDest)
    {
        logInfo("the Cx connection towards HSS is not available.");
        rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    diaCxSarAppInput_t sarInput = {NULL, &pSessInfo->tempWorkInfo.users[0], &serverName, &sarInfo, 1 << DIA_CX_FEATURE_LIST_ID_SIFC, NULL};
    status = diaCx_sendSAR(&sarInput, scscfSessStateSar_onSaaMsg, pSessInfo);
    if(status != OS_STATUS_OK)
    {
        logError("fails to diaCx_initSAR for impu(%r) for DIA_3GPP_CX_UNREGISTERED_USER.", &pSessInfo->tempWorkInfo.users[0]);
        rspCode = SIP_RESPONSE_500;
        goto EXIT;
    }


EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	return status;
}


static void scscfSessStateSar_onSaaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData)
{
	osStatus_e status = OS_STATUS_OK;
    scscfSessInfo_t* pSessInfo = pAppData;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    pSessInfo->pRegInfo = scscfReg_onSessSaa(pDiaDecoded, &rspCode, &pSessInfo->sIfcIdList);
    if(!pSessInfo->pRegInfo)
    {
        logError("fail to scscfReg_onSessSaa for sip user(%r), isMO=%d, sessInfo(%p).", &pSessInfo->tempWorkInfo.users[0], pSessInfo->tempWorkInfo.isMO, pSessInfo);
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //check if any user is barred
    osPointerLen_t* pBarredUser = scscfReg_getAnyBarredUser(pSessInfo->pRegInfo, pSessInfo->tempWorkInfo.users, pSessInfo->tempWorkInfo.userNum);
    if(pBarredUser)
    {
        logInfo("a user(%r) is barred, callId=%r, isMO=%d, total user num=%d", pBarredUser, &pSessInfo->tempWorkInfo.callId, pSessInfo->tempWorkInfo.isMO, pSessInfo->tempWorkInfo.userNum);
        rspCode = SIP_RESPONSE_403;
        status = OS_ERROR_EXT_INVALID_VALUE;
        goto EXIT;
    }

	status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_DNS_AS);

EXIT:
	//no need to osfree pDiaDecoded since app did not ref it
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
	}

    if(status != OS_STATUS_OK && pSessInfo)
    {
        scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
    }

	return;
}


static osStatus_e scscfSessStateDnsAs_onMsg(scscfSessInfo_t* pSessInfo)
{
	osStatus_e status = OS_STATUS_OK;
    dnsResResponse_t* pDnsResponse = NULL;
	sipResponse_e rspCode = SIP_RESPONSE_200;

    scscfIfcEvent_t ifcEvent = {pSessInfo->tempWorkInfo.pSipTUMsg->sipMsgBuf.reqCode, scscfIfc_getSessCase(pSessInfo->userRegState, pSessInfo->tempWorkInfo.isMO), SCSCF_IFC_REGISTRATION_TYPE_INITIAL};

    osPointerLen_t* pAS = scscfIfc_getNextAS(&pSessInfo->tempWorkInfo.pLastIfc, &pSessInfo->sIfcIdList, pSessInfo->tempWorkInfo.pReqDecodedRaw, &ifcEvent, &pSessInfo->tempWorkInfo.isContinuedDH);
    if(!pAS)
    {
        mdebug(LM_CSCF, "no more AS, forward request to AS for user(%r) is completed.", &pSessInfo->tempWorkInfo.users[0]);
		if(pSessInfo->tempWorkInfo.isMO)
		{
        	status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_MO2MT_INIT);
		}
		else
		{
			status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_UE);
		}
        goto EXIT;
    }

    mdebug(LM_CSCF, "next hop is %r", pAS);

    sipTuUri_t targetUri = {{*pAS}, true};
    sipTu_convertUri2NextHop(&targetUri, &pSessInfo->tempWorkInfo.nextHop);

    //if nextHop is FQDN, perform DNS query
    if(osIsIpv4(&pSessInfo->tempWorkInfo.nextHop.nextHop.ipPort.ip))
    {
		sipTuRR_t* pOwnRR = cscf_buildOwnRR(&pSessInfo->tempWorkInfo.users[0], cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF));
		sipProxyRouteModCtl_t routeModCtl = {&pSessInfo->tempWorkInfo.nextHop.nextHop, pOwnRR};
		scscf_buildReqModInfo(pSessInfo, pSessInfo->tempWorkInfo.isInitial, pSessInfo->tempWorkInfo.pReqDecodedRaw, &pSessInfo->tempWorkInfo.nextHop.nextHopRaw, cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF), pOwnRR, &routeModCtl.msgModInfo);

		proxyInfo_t* pProxyInfo = NULL;
		sipProxyAppInfo_t appInfo = {scscfSess_onSipRsp, pSessInfo};
	    status = proxy_onSipTUMsgViaApp(SIP_TU_MSG_TYPE_MESSAGE, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, &routeModCtl, &pProxyInfo, &appInfo);
    	if(pProxyInfo)
    	{
    		osList_append(&pSessInfo->proxyList, osmemref(pProxyInfo));
			pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = osPlHash_addUserData(gSessHash, &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl, true, pSessInfo);
		}
		else
		{	
			if(pSessInfo->tempWorkInfo.isContinuedDH)
			{
        		status = scscfSessStateDnsAs_onMsg(pSessInfo);
			}
	        else
    	    {
				//fails to create proxy
				rspCode = SIP_RESPONSE_500;
        	}
    	}

		goto EXIT;
	}

	//AS is a FQDN, need to resolve to IP address
    dnsQueryStatus_e dnsQueryStatus = dnsQuery(&pSessInfo->tempWorkInfo.nextHop.nextHop.ipPort.ip, pSessInfo->tempWorkInfo.nextHop.nextHop.ipPort.port ? DNS_QTYPE_A : DNS_QTYPE_NAPTR, true, true, &pDnsResponse, scscfSessState_dnsCallback, pSessInfo);
	if(dnsQueryStatus == DNS_QUERY_STATUS_FAIL)
	{
		logError("fails to perform dns query for %r.", &pSessInfo->tempWorkInfo.nextHop.nextHop.ipPort.ip);
		if(pSessInfo->tempWorkInfo.isContinuedDH)
        {
        	status = scscfSessStateDnsAs_onMsg(pSessInfo);
		}
		else
		{
			//fails to resolve AS address
			rspCode = SIP_RESPONSE_500;
			status = OS_ERROR_SYSTEM_FAILURE;
		}

		goto EXIT;
	}

	//waiting for dns query response
	if(pDnsResponse)
	{
		status = scscfSessStateDnsAs_fwdRequest(pSessInfo, pDnsResponse, true);
		if(status != OS_STATUS_OK)
		{
			rspCode = SIP_RESPONSE_500;
		}
#if 0
        //for now, assume tcp and udp always use the same port.  improvement can be done to allow different port, in this case, sipProxy_forwardReq() pNextHop needs to pass in both tcp and udp nextHop info for it to choose, like based on message size, etc. to-do
        if(!sipTu_getBestNextHop(pDnsResponse, true, &pSessInfo->tempWorkInfo.nextHop))
        {
            logError("could not find the next hop for %r.", &pSessInfo->tempWorkInfo.nextHop.ipPort.ip);
			if(pSessInfo->tempWorkInfo.isContinuedDH)
			{
            	status = scscfSessStateDnsAs_onMsg(pSessInfo);
			}
            else
            {
				rspCode = SIP_RESPONSE_500;
            }
            goto EXIT;
        }
#endif
    }

EXIT:
	if(rspCode != SIP_RESPONSE_200)
	{
		sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
	}

	osfree(pDnsResponse);

	return status;
}


static osStatus_e scscfSessStateDnsAs_fwdRequest(scscfSessInfo_t* pSessInfo, dnsResResponse_t* pRR, bool isCachedDnsRsp)
{
	osStatus_e status = OS_STATUS_OK;

	sipTuAddr_t nextHop = {};
    //for now, assume tcp and udp always use the same port.  improvement can be done to allow different port, in this case, sipProxy_forwardReq() pNextHop needs to pass in both tcp and udp nextHop info for it to choose, like based on message size, etc. to-do
    if(!sipTu_getBestNextHop(pRR, isCachedDnsRsp, &nextHop, NULL))
    {
		logError("could not find the next hop for %r.", &nextHop.ipPort.ip);
        if(pSessInfo->tempWorkInfo.isContinuedDH)
        {
        	status = scscfSessStateDnsAs_onMsg(pSessInfo);
        }
        else
        {
            status = OS_ERROR_NETWORK_FAILURE;
        }

        goto EXIT;
    }

    mdebug(LM_CSCF, "send request to %A.", &nextHop.sockAddr);

    sipTuRR_t* pOwnRR = cscf_buildOwnRR(&pSessInfo->tempWorkInfo.users[0], cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF));
    sipProxyRouteModCtl_t routeModCtl = {&nextHop, pOwnRR};
    scscf_buildReqModInfo(pSessInfo, pSessInfo->tempWorkInfo.isInitial, pSessInfo->tempWorkInfo.pReqDecodedRaw, &pSessInfo->tempWorkInfo.nextHop.nextHopRaw, cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF), pOwnRR, &routeModCtl.msgModInfo);

    proxyInfo_t* pProxyInfo = NULL;
	sipProxyAppInfo_t appInfo = {scscfSess_onSipRsp, pSessInfo};
    status = proxy_onSipTUMsgViaApp(SIP_TU_MSG_TYPE_MESSAGE, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, &routeModCtl, &pProxyInfo, &appInfo);
    if(pProxyInfo)
    {
        osList_append(&pSessInfo->proxyList, osmemref(pProxyInfo));
        pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = osPlHash_addUserData(gSessHash, &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl, true, pSessInfo);
    }
    else
    {
		if(pSessInfo->tempWorkInfo.isContinuedDH)
       	{
    		status = scscfSessStateDnsAs_onMsg(pSessInfo);
        }
        else
        {
			status = OS_ERROR_NETWORK_FAILURE;
		}

        goto EXIT;
    }

EXIT:
	return status;
}


static osStatus_e scscfSessStateMTInit_onMsg(scscfSessInfo_t* pSessInfo)
{
	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    pSessInfo->pRegInfo = scscfReg_getRegInfo(&pSessInfo->tempWorkInfo.users[0], &pSessInfo->userRegState, &pSessInfo->sIfcIdList);
    if(!pSessInfo->pRegInfo)
    {
        lbAnchorInfo_t* pAnchorInfo = lb_getAnchorInfo(&pSessInfo->tempWorkInfo.users[0]);
        //lb finds the anchor info for the MT user, must be another SCSCF, forward to the SCSCF via lb
        if(pAnchorInfo)
        {
            //update the request(like remove the top route, etc.) and send to LB via queue, to-do
	        scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_LOCAL_CSCF);
        }
		else
		{
        	// for this SCSCF and NOT_REGISTERED case, perform SAR to download user profile.
        	status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_SAR);
		}
        goto EXIT;
    }

    //there is regInfo for the user, but the state is SCSCF_REG_STATE_NOT_REGISTERED, implies the user is doing initial registration
    if(pSessInfo->userRegState == SCSCF_REG_STATE_NOT_REGISTERED)
    {
        logInfo("the user(%r) is performing initial registration, reject the request.", &pSessInfo->tempWorkInfo.users[0]);
        rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //check if any user is barred
    osPointerLen_t* pBarredUser = scscfReg_getAnyBarredUser(pSessInfo->pRegInfo, pSessInfo->tempWorkInfo.users, pSessInfo->tempWorkInfo.userNum);
    if(pBarredUser)
    {
        logInfo("a user(%r) is barred, isMO=%d, total user num=%d", pBarredUser, pSessInfo->tempWorkInfo.isMO, pSessInfo->tempWorkInfo.userNum);
        rspCode = SIP_RESPONSE_403;
        status = OS_ERROR_EXT_INVALID_VALUE;
        goto EXIT;
    }

    logInfo("create a session(%p), isMO=%d, user=%r, callId=%r", pSessInfo, pSessInfo->tempWorkInfo.isMO, &pSessInfo->tempWorkInfo.users[0], &pSessInfo->tempWorkInfo.callId);

    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_DNS_AS);

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

    return status;
}


static osStatus_e scscfSessStateMO2MTInit_onMsg(scscfSessInfo_t* pSessInfo)
{
	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    pSessInfo->tempWorkInfo.isMO = false;
	pSessInfo->tempWorkInfo.pLastIfc = NULL;
	pSessInfo->userRegState = SCSCF_REG_STATE_UN_REGISTERED;	//default setting. if the user is in other state, later scscfReg_getRegInfo() will set it.

    //get the MT user
    pSessInfo->tempWorkInfo.userNum = 1;
    status = cscf_getRequestUser(pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, false, pSessInfo->tempWorkInfo.users, &pSessInfo->tempWorkInfo.userNum);
    if(status != OS_STATUS_OK)
    {
        logError("fails to cscf_getRequestUser for callId(%r).", &pSessInfo->tempWorkInfo.callId);
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }
	mdebug(LM_CSCF, "the MT user is:%r", &pSessInfo->tempWorkInfo.users[0]);

    /* need to check the MT is in-net or out of net.
     * first try to get regInfo from the same SCSCF's registrar, if found, must be in-net, otherwise, may be out of net
     */
    pSessInfo->pRegInfo = scscfReg_getRegInfo(&pSessInfo->tempWorkInfo.users[0], &pSessInfo->userRegState, &pSessInfo->sIfcIdList);
    //there is regInfo
    if(pSessInfo->pRegInfo)
    {
		if(pSessInfo->userRegState == SCSCF_REG_STATE_NOT_REGISTERED)
		{
			//to-do, the user is performing initial registration, simply reject the request, expect the MO user to recall
		}
		else
		{
			scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_DNS_AS);
		}
		goto EXIT;
	}

	//check if the user is in the local SCSCF
    lbAnchorInfo_t* pAnchorInfo = lb_getAnchorInfo(&pSessInfo->tempWorkInfo.users[0]);
    //lb finds the anchor info for the MT user, must be another SCSCF, forward to the SCSCF via lb
    if(pAnchorInfo)
    {
        //update the request(like remove the top route, etc.) and send to LB via queue, to-do
		scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_LOCAL_CSCF);
        goto EXIT;
    }

    /* at this point, this SCSCF does not have regInfo for the MT user, neither the local SCSCF.  the MT user is either in-net, 
	 * but not registered, or in-net, but registered in geo SCSCF, or is out of net perform DNS ENUM to see if the MT user is 
	 * in-net or out of net
	 */
	scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_DNS_ENUM);

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	return status;
}


static osStatus_e scscfSessStateDnsEnum_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    sipPointerLen_t qName=SIPPL_INIT(qName);
    cscf_getEnumQName(&pSessInfo->tempWorkInfo.users[0], &qName.pl);
    dnsResResponse_t* pDnsResponse = NULL;
    dnsQueryStatus_e qStatus = dnsQuery(&qName.pl, DNS_QTYPE_NAPTR, false, true, &pDnsResponse, scscfSessState_dnsCallback, pSessInfo);
    if(qStatus == DNS_QUERY_STATUS_FAIL)
    {
        logError("fails to perform dns query for %r.", &qName.pl);
        rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    //dns query ongoing
    if(!pDnsResponse)
    {
        //waiting for dns response, we still do not know if the MT user is in or outof net, if in-net, if the user is not register or in geo SCSCF
        //these need  be checked after the dns response returns back
        goto EXIT;
    }

    //Get DNS response, must be a MT in-network, perform LIR to determine where the MT shall be anchored
    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_LIR);

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	osfree(pDnsResponse);

	return status;
}


static osStatus_e scscfSessStateLir_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

	//request ICSCF to perform LIR. since the call is initiated by scscf, do not expect to return SCSCF capabilities 
	icscf_performLir4Scscf(&pSessInfo->tempWorkInfo.users[0], scscfSessStateLir_onLia, pSessInfo);

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	return status;
}


static void scscfSessStateLir_onLia(diaMsgDecoded_t* pDiaDecoded, void* pAppData)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;
	scscfSessInfo_t* pSessInfo = pAppData;

	if(!pAppData)
	{
        logError("null pointer, pAppData.");
        goto EXIT;
    }

	osPointerLen_t scscfName = {};
	diaResultCode_t resultCode = {};
	status = icscf_decodeLia(pDiaDecoded, &scscfName, NULL, &resultCode);
	if(status != OS_STATUS_OK)
	{
		logError("fails to decode LIR.");
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//if resultCode.isResultCode
	if(resultCode.isResultCode)
	{
		if(resultCode.resultCode != 2000)
		{
			logInfo("resultCode=%d.", resultCode.resultCode);
			rspCode = cscf_cx2SipRspCodeMap(resultCode);
			status = OS_ERROR_INVALID_VALUE;
    	}
		else
		{
			if(cscfConfig_isOwnScscf(&scscfName))
			{
		        lbAnchorInfo_t* pAnchorInfo = lb_getAnchorInfo(&pSessInfo->tempWorkInfo.users[0]);
        		//lb finds the anchor info for the MT user, must be another SCSCF, forward to the SCSCF via lb
        		if(pAnchorInfo)
        		{
            		//update the request(like remove the top route, etc.) and send to LB via queue, to-do
					status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_LOCAL_CSCF);
        		}
        		else
        		{
            		// for this SCSCF and NOT_REGISTERED case, perform SAR to download user profile.
            		status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_SAR);
				}
			}
			else
			{
				status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_GEO_SCSCF);
			}
		}

		goto EXIT;
	}

	//if resultCode is a experimental code
	if(resultCode.expCode < 3000 && resultCode.expCode >= 2000)
	{
		if(!scscfName.l)
		{
			if(resultCode.expCode == DIA_CX_EXP_RESULT_UNREGISTERED_SERVICE || resultCode.expCode == DIA_CX_EXP_RESULT_SUCCESS_SERVER_NAME_NOT_STORED)
			{
                lbAnchorInfo_t* pAnchorInfo = lb_getAnchorInfo(&pSessInfo->tempWorkInfo.users[0]);
                //lb finds the anchor info for the MT user, must be another SCSCF, forward to the SCSCF via lb
                if(pAnchorInfo)
                {
                    //update the request(like remove the top route, etc.) and send to LB via queue, to-do
                    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_LOCAL_CSCF);
                }
                else
                {
                    // for this SCSCF and NOT_REGISTERED case, perform SAR to download user profile.
                    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_SAR);
                }
			}
			else
			{
				logError("expCode=%d, but LIA does not contain server name.", resultCode.expCode);
				status = OS_ERROR_SYSTEM_FAILURE;
			}
		}
		else
		{
            if(cscfConfig_isOwnScscf(&scscfName))
            {
                lbAnchorInfo_t* pAnchorInfo = lb_getAnchorInfo(&pSessInfo->tempWorkInfo.users[0]);
                //lb finds the anchor info for the MT user, must be another SCSCF, forward to the SCSCF via lb
                if(pAnchorInfo)
                {
                    //update the request(like remove the top route, etc.) and send to LB via queue, to-do
                    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_LOCAL_CSCF);
                }
                else
                {
                    // for this SCSCF and NOT_REGISTERED case, perform SAR to download user profile.
                    status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_SAR);
                }
            }
            else
            {
                status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_GEO_SCSCF);
            }
        }
	}
	else
	{
		logInfo("resultCode=%d.", resultCode.resultCode);
        rspCode = cscf_cx2SipRspCodeMap(resultCode);
        status = OS_ERROR_INVALID_VALUE;
    }
		
	goto EXIT;

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	if(status != OS_STATUS_OK)
	{
        //to-do, check other state, if status != OS_STATUS_OK, and is the start of a thread, enter the closing state
        scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
	}

	
	return;
}


static osStatus_e scscfSessStateToLocalSCSCF_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

	//to send the message to the other SCSCF working thread via lb. to-do	

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

    return status;
}


//breakout can implement all sort of logic to select a particular destination based on the received SIP message and local policy.
//for now, just simply send to a pre-configured address.
static osStatus_e scscfSessStateToBreakout_onMsg(scscfSessInfo_t* pSessInfo)
{
	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    sipTuAddr_t mgcpAddr;
	cscfConfig_getMgcpAddr(&mgcpAddr);

    //if nextHop is FQDN, perform DNS query
    if(osIsIpv4(&mgcpAddr.ipPort.ip))
    {
		sipPointerLen_t mgcpRoute = SIPPL_INIT(mgcpRoute);
		mgcpRoute.pl.l = osPrintf_buffer((char*)mgcpRoute.pl.p, SIP_HDR_MAX_SIZE, "<sip: %r:%d; lr>", &mgcpAddr.ipPort.ip, mgcpAddr.ipPort.port);
        sipTuRR_t* pOwnRR = cscf_buildOwnRR(&pSessInfo->tempWorkInfo.users[0], cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF));
        sipProxyRouteModCtl_t routeModCtl = {&mgcpAddr, pOwnRR};
        scscf_buildReqModInfo(pSessInfo->pRegInfo, pSessInfo->tempWorkInfo.isInitial, pSessInfo->tempWorkInfo.pReqDecodedRaw, &mgcpRoute.pl, NULL, pOwnRR, &routeModCtl.msgModInfo);

		proxyInfo_t* pProxyInfo = NULL;
	    sipProxyAppInfo_t appInfo = {scscfSess_onSipRsp, pSessInfo};
        status = proxy_onSipTUMsgViaApp(SIP_TU_MSG_TYPE_MESSAGE, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, &routeModCtl, &pProxyInfo, &appInfo);
        if(pProxyInfo)
        {
            osList_append(&pSessInfo->proxyList, osmemref(pProxyInfo));
        	pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = osPlHash_addUserData(gSessHash, &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl, true, pSessInfo);
        }
        else
        {
			//fails to create proxy
			rspCode = SIP_RESPONSE_500;
        }

        goto EXIT;
    }
	else
	{
		logError("mgcpAddr is a FQDN(%r).", &mgcpAddr.ipPort.ip);
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
	}

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	return status;
}


static osStatus_e scscfSessStateToGeoSCSCF_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

    return status;
}


static osStatus_e scscfSessStateToUe_start(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

    if(pSessInfo->userRegState != SCSCF_REG_STATE_REGISTERED)
	{
		rspCode = SIP_RESPONSE_480;
		goto EXIT;
	}

	sipTuAddr_t nextHop = {};
	scscfReg_getUeContact(pSessInfo->pRegInfo, &nextHop);
	sipTuRR_t* pOwnRR = cscf_buildOwnRR(&pSessInfo->tempWorkInfo.users[0], cscfConfig_getOwnAddr(CSCF_TYPE_SCSCF));
    sipProxyRouteModCtl_t routeModCtl = {&nextHop, pOwnRR};
    scscf_buildReqModInfo(pSessInfo->pRegInfo, pSessInfo->tempWorkInfo.isInitial, pSessInfo->tempWorkInfo.pReqDecodedRaw, NULL, NULL, pOwnRR, &routeModCtl.msgModInfo);

	proxyInfo_t* pProxyInfo = NULL;
    sipProxyAppInfo_t appInfo = {scscfSess_onSipRsp, pSessInfo};
    status = proxy_onSipTUMsgViaApp(SIP_TU_MSG_TYPE_MESSAGE, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, &routeModCtl, &pProxyInfo, &appInfo);
    if(pProxyInfo)
    {
    	osList_append(&pSessInfo->proxyList, osmemref(pProxyInfo));
        pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = osPlHash_addUserData(gSessHash, &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl, true, pSessInfo);
    }
    else
	{
		rspCode = SIP_RESPONSE_500;
	}

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

    return status;
}


static osStatus_e scscfSessStateEstablished_onMsg(scscfSessInfo_t* pSessInfo)
{
    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_200;

EXIT:
    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

    return status;
}


static osStatus_e scscfSessStateClosing_onMsg(scscfSessInfo_t* pSessInfo)
{
	/* just need to cleanup workInfo, for the current working proxy, it is either not created or deleted by
     * the function generating the bad status.  for other proxies, each time a proxy is removed due to say,
     * receiving a error response, scscfSess will be notified and the proxy will be deleted at that time
     */
    scscfSessInfo_workInfoCleanup(pSessInfo);

	//if there is no more proxy for the session, free the session, otherwise, the removal of proxy will notify to free the session
    if(osList_isEmpty(&pSessInfo->proxyList))
    {
		osListElement_t* pLE = pSessInfo->callIdHashElemList.head;
		while(pLE)
		{
			osHash_deleteNode(pLE->data, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
			pLE = pLE->next;
		}

        osfree(pSessInfo);
    }

	return OS_STATUS_OK;
}


static void scscfSessStateClosing_onProxyDelete(scscfSessInfo_t* pSessInfo, proxyInfo_t* pProxy)
{        
	osListElement_t* pLE = pSessInfo->proxyList.head;
	while(pLE)
    {
        if(pLE->data == pProxy)
        {
            osList_deleteElementAll(pLE, true);
            break;
        }

		pLE = pLE->next;
	}

	if(osList_isEmpty(&pSessInfo->proxyList))
    {
        osListElement_t* pLE = pSessInfo->callIdHashElemList.head;
        while(pLE)
        {
            osHash_deleteNode(pLE->data, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
            pLE = pLE->next;
        }

        osfree(pSessInfo);
    }

	return;
}


static osStatus_e scscfSess_enterState(scscfSessInfo_t* pSessInfo, scscfSessState_e newSessState)
{
	osStatus_e status = OS_STATUS_OK;
	scscfSessState_e oldState = pSessInfo->state;
	pSessInfo->state = newSessState;
	mdebug(LM_CSCF, "change sessState from %d to %d for user(%r).", oldState, newSessState, &pSessInfo->tempWorkInfo.users[0]);
 
	switch(newSessState)
	{
		case SCSCF_SESS_STATE_MO_INIT:
			status = scscfSessStateMoInit_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_MT_INIT:
			status = scscfSessStateMTInit_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_SAR:
			if(oldState != SCSCF_SESS_STATE_MO_INIT && oldState != SCSCF_SESS_STATE_MT_INIT && oldState != SCSCF_SESS_STATE_LIR)
			{
				logError("unexpected old state(%d) for new state SCSCF_SESS_STATE_SAR.", oldState);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			status = scscfSessStateSar_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_DNS_AS:
			status = scscfSessStateDnsAs_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_MO2MT_INIT:
			status = scscfSessStateMO2MTInit_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_DNS_ENUM:
			status = scscfSessStateDnsEnum_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_LIR:
			status = scscfSessStateLir_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_TO_LOCAL_CSCF:
			status = scscfSessStateToLocalSCSCF_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_TO_GEO_SCSCF:
			status = scscfSessStateToGeoSCSCF_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_TO_UE:
			status = scscfSessStateToUe_start(pSessInfo);
			break;
		case SCSCF_SESS_STATE_TO_BREAKOUT:
			status = scscfSessStateToBreakout_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_ESTABLISHED:
			status = scscfSessStateEstablished_onMsg(pSessInfo);
			break;
		case SCSCF_SESS_STATE_CLOSING:
			scscfSessStateClosing_onMsg(pSessInfo);
 			break;
		default:
			break;
    }

EXIT:
	return status;
}
			

static void scscfSessState_dnsCallback(dnsResResponse_t* pRR, void* pData)
{
	osStatus_e status = OS_STATUS_OK;
	sipResponse_e rspCode = SIP_RESPONSE_200;
    bool isForwardReq2AsDone = false;
    if(!pRR || !pData)
    {
        logError("null pointer, pRR=%p, pData=%p.", pRR, pData);
        return;
    }

    scscfSessInfo_t* pSessInfo = pData;

	switch(pSessInfo->state)
	{
		case SCSCF_SESS_STATE_DNS_ENUM:
		{
			//got no error dns response, the MT user is in-net, let ICSCF to perform LIR to determine if the MT user is registered in geo SCSCF or not registered
			if(dnsResolver_isRspNoError(pRR))
			{
				scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_LIR);	

				goto EXIT;
			}

			if(pRR->rrType == DNS_RR_DATA_TYPE_STATUS && pRR->status.resStatus != DNS_RES_STATUS_OK)
			{
				rspCode = SIP_RESPONSE_500; 
				status = OS_ERROR_NETWORK_FAILURE;
			}
			else
			{
				//if we get here, the MT user is out of network, send the request to the final destionation
				status = scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_TO_BREAKOUT);
			}

			break;
        }
		case SCSCF_SESS_STATE_DNS_AS:
		{
    		if(pRR->rrType == DNS_RR_DATA_TYPE_STATUS)
    		{
        		logInfo("dns query failed, qName=%r, resStatus=%d, dnsRCode=%d", pRR->status.pQName, pRR->status.resStatus, pRR->status.dnsRCode);

				if(pSessInfo->tempWorkInfo.isContinuedDH)
				{
					status = scscfSessStateDnsAs_onMsg(pSessInfo);
        			goto EXIT;
				}
				else
				{
					rspCode = SIP_RESPONSE_500;
					status = OS_ERROR_NETWORK_FAILURE;
				}

				goto EXIT;	
    		}

			status = scscfSessStateDnsAs_fwdRequest(pSessInfo, pRR, true);
			if(status != OS_STATUS_OK)
			{
				rspCode = SIP_RESPONSE_500;
			}

			break;
		}
		default:
			logError("unexpected state(%d), drop the dns response.", pSessInfo->state);
			status = OS_ERROR_INVALID_VALUE;
			break;
	}

EXIT:
    osfree(pRR);

    if(rspCode != SIP_RESPONSE_200)
    {
        sipProxy_uasResponse(rspCode, pSessInfo->tempWorkInfo.pSipTUMsg, pSessInfo->tempWorkInfo.pReqDecodedRaw, pSessInfo->tempWorkInfo.pSipTUMsg->pTransId, NULL);
    }

	if(status != OS_STATUS_OK)
	{
		scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
	}

	return;
}


static void scscfSess_onProxyStatus(void* pProxyMgrInfo, proxyInfo_t* pProxy, proxyStatus_e proxyStatus)
{
    osStatus_e status = OS_STATUS_OK;

    mdebug(LM_CSCF, "proxy(%p) notifies new proxy status(%d) for pSessInfo(%p).", pProxy, proxyStatus, pProxyMgrInfo);

	scscfSessInfo_t* pSessInfo = pProxyMgrInfo;

    switch(proxyStatus)
    {
        case SIP_PROXY_STATUS_ESTABLISHED:
			switch(pSessInfo->state)
			{
				case SCSCF_SESS_STATE_TO_LOCAL_CSCF:
				case SCSCF_SESS_STATE_TO_BREAKOUT:
				case SCSCF_SESS_STATE_TO_GEO_SCSCF:
				case SCSCF_SESS_STATE_TO_UE:
				case SCSCF_SESS_STATE_DNS_AS:
            		if(pSessInfo->proxyList.head->data == pProxy)
            		{
						scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_ESTABLISHED);
					}
					
                	//now check if there are proxies that does not require record route, and remove them from the proxy list + update the impacted proxies
                	//to-do
					break;
				default:
					logError("received SIP_PROXY_STATUS_ESTABLISHED in unexpected sessState(%d) for session(%p).", pSessInfo->state, pSessInfo);
					scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
					break;
            }
            break;
        case SIP_PROXY_STATUS_DELETE:
        {
			//this can potentially receive in any state after state DNS_AS
			if(pSessInfo->state != SCSCF_SESS_STATE_CLOSING)
			{
				scscfSess_enterState(pSessInfo, SCSCF_SESS_STATE_CLOSING);
				break;
			}

			scscfSessStateClosing_onProxyDelete(pSessInfo, pProxy);

            break;
        }
        default:
            logError("unexpected proxyStatus(%d).", proxyStatus);
            status = OS_ERROR_INVALID_VALUE;
            break;
    }

EXIT:
    return;
}


static osStatus_e scscfSess_isInitialReq(scscfSessInfo_t* pSessInfo, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool* isInitialReq)
{
    osStatus_e status = OS_STATUS_OK;
    *isInitialReq = false;

    sipHdrDecoded_t sipHdrDecoded = {};
    sipHdrGenericNameParam_t* pTopRoute = sipDecodeTopRouteValue(pReqDecodedRaw, &sipHdrDecoded, false);
    if(!pTopRoute)
    {
        logError("receive a request without route, but there is already a hashed session for the callId(%r).", &pSessInfo->tempWorkInfo.callId);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(osPL_cmp(&pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl, &pTopRoute->uri.userInfo.sipUser.user) != 0)
    {
        logError("receive a request with callId(%r), the user of the top route=%r, expect odi=%r.", &pSessInfo->tempWorkInfo.callId, &pTopRoute->uri.userInfo.sipUser.user, &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    *isInitialReq = true;

EXIT:
    osfree(sipHdrDecoded.decodedHdr);
    return status;
}


//find an existing proxy for a subsequent request based on the sip message received
static proxyInfo_t* scscfSess_getMatchingProxy(scscfSessInfo_t* pSessInfo, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw)
{
	proxyInfo_t* pProxyInfo = NULL;
    sipHdrDecoded_t sipHdrDecoded = {};

	if(!pSessInfo || !pSipTUMsg || !pReqDecodedRaw)
	{
		logError("null pointer, pSessInfo=%p, pSipTUMsg=%p, pReqDecodedRaw=%p.", pSessInfo, pSipTUMsg, pReqDecodedRaw);
		goto EXIT;
	}

	if(!pSipTUMsg->sipMsgBuf.isRequest)		
	{
		logInfo("the sip message is not a request.");
		goto EXIT;
	}

	//the identified proxy shall match with the user part of the top route in the received message
	sipHdrGenericNameParam_t* pTopRoute = sipDecodeTopRouteValue(pReqDecodedRaw, &sipHdrDecoded, false);
	if(!pTopRoute)
	{
		logInfo("the received sip message does not exist route header.");
		goto EXIT;
	}

	osPointerLen_t* pProxyId = &pTopRoute->uri.userInfo.sipUser.user;
	osListElement_t* pLE = pSessInfo->proxyList.head;
	while(pLE)
	{
		if(osPL_cmp(pProxyId, &((proxyInfo_t*)pProxyInfo)->pOwnRR->user) == 0)
		{
			pProxyInfo = pLE->data;
			goto EXIT;
		}

		pLE = pLE->next;
	}

EXIT:
    osfree(sipHdrDecoded.decodedHdr);

	return pProxyInfo;
}


//isOrig: true, the original request from UE or AS, i.e., not request with the original dialogue identifier (ODI).
//        false, the initial request that has ODI (an indication that this message was forwarded by the SCSCF before)
//        note: isOrig here does not mean the request is from UE, merely mean this message has not been forwarded by SCSCF
static osStatus_e scscf_buildReqModInfo(scscfSessInfo_t* pSessInfo, bool isOrig, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pAS, sipTuAddr_t* pScscfAddr, sipTuRR_t* pRRScscf, sipProxy_msgModInfo_t* pMsgModInfo)
{
	osStatus_e status = OS_STATUS_OK;

	pMsgModInfo->isAuto = false;
	pMsgModInfo->isChangeCallId = false;
	pMsgModInfo->addNum = 0;
	pMsgModInfo->delNum = 0;

	osPointerLen_t* pSipPLHdr = NULL;
	if(pScscfAddr)
	{
		pSipPLHdr = sipProxyMsgModInfo_addSipPLHdr(pMsgModInfo->extraAddHdr, &pMsgModInfo->addNum, SIP_HDR_ROUTE);
		pSipPLHdr->l = osPrintf_buffer((char*)pSipPLHdr->p, SIP_HDR_MAX_SIZE, "<sip:%r@%r:%d; lr>", scscfSess_createOdi(pSessInfo), &pScscfAddr->ipPort.ip, pScscfAddr->ipPort.port);
	}

    if(pAS)
    {
        osPointerLen_t* pSipPLHdr = sipProxyMsgModInfo_addSipPLHdr(pMsgModInfo->extraAddHdr, &pMsgModInfo->addNum, SIP_HDR_ROUTE);
        pSipPLHdr->l = osPrintf_buffer((char*)pSipPLHdr->p, SIP_HDR_MAX_SIZE, "<%r>", pAS);
    }

	if(isOrig)
	{
        //always try to get both sip and tel URI if possible as PSI
        osPointerLen_t noBarredPai[SCSCF_MAX_PAI_NUM];
        int noBarredPaiNum = 0;
		if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY])
		{
	        sipIdentity_t paiUser[SCSCF_MAX_PAI_NUM];
    	    int userNum = 0;
        	int uriType = 0;

			status = sipDecode_getMGNPHdrURIs(SIP_HDR_P_ASSERTED_IDENTITY, pReqDecodedRaw, paiUser, &userNum);
        	if(status != OS_STATUS_OK)
        	{
            	logError("fails to sipDecode_getMGNPHdrURIs for SIP_HDR_P_ASSERTED_IDENTITY.");
            	goto EXIT;
        	}

			//remove any barred user
			int validUserNum = userNum;
			for(int i=0; i<userNum; i++)
			{
				if(scscfReg_isUserBarred(pSessInfo->pRegInfo, &paiUser[i].sipUser))
				{
					paiUser[i].sipUser.l = 0;
					validUserNum--;
					continue;
				}

				uriType |= 1<<paiUser[i].sipUriType;	
			}

			if(validUserNum == 0)
			{
				bool isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, true, noBarredPai);
				noBarredPaiNum = isFound ? 1 : 0;
				isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, false, &noBarredPai[noBarredPaiNum]);
				noBarredPaiNum = isFound ? ++noBarredPaiNum : noBarredPaiNum;
			}
			else
			{
				for(int i=0; i<userNum; i++)
                {
                	if(paiUser[i].sipUser. l == 0)
                    {
                        continue;
                    }
                    noBarredPai[noBarredPaiNum++] = paiUser[i].sipUser;
                }

 				if(noBarredPaiNum == 1)
				{
					bool isFoundSipUri = (uriType & URI_TYPE_TEL) ? true :false;
					bool isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, isFoundSipUri, &noBarredPai[1]);			
					noBarredPaiNum = isFound ? 2 : 1;
				}
				else
				{
					if((uriType & URI_TYPE_SIP) && (uriType & URI_TYPE_TEL))
					{
						//has both sip and tel, do nothing here
					} 
					else
					{
						bool isFoundSipUri = (uriType & URI_TYPE_TEL) ? true :false;
						bool isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, isFoundSipUri, &noBarredPai[noBarredPaiNum]);
                    	noBarredPaiNum = isFound ? ++noBarredPaiNum : noBarredPaiNum;
					}
				}
			}
		}
		else
		{
            bool isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, true, noBarredPai);
            noBarredPaiNum = isFound ? 1 : 0;
            isFound = scscfReg_getOneNoBarUri(pSessInfo->pRegInfo, false, &noBarredPai[noBarredPaiNum]);
            noBarredPaiNum = isFound ? ++noBarredPaiNum : noBarredPaiNum;
		}	
		for(int i=0; i<noBarredPaiNum; i++)
		{					
			sipTuHdrRawValue_t value = {SIPTU_RAW_VALUE_TYPE_STR_OSPL, {.strValue = noBarredPai[i]}};
			sipProxyMsgModInfo_addHdr(pMsgModInfo->extraAddHdr, &pMsgModInfo->addNum, SIP_HDR_P_ASSERTED_IDENTITY, &value);
		}

		if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_PREFERRED_IDENTITY])
		{
			sipProxyMsgModInfo_delHdr(pMsgModInfo->extraDelHdr, &pMsgModInfo->delNum, SIP_HDR_P_PREFERRED_IDENTITY, false);
		}

		if(!pReqDecodedRaw->msgHdrList[SIP_HDR_P_CHARGING_VECTOR])
		{
			//to-do, to change
			sipPointerLen_t chargeTemp = SIPPL_INIT(chargeTemp);
			chargeTemp.pl.l = sizeof("icid-value=ECE-CLF-ece01-pl01-clf-1550683191255-712;icid-generated-at=ECE-CLF-ece01-pl01-clf-ece01-pl01-clf;orig-ioi=ims.globalstar.com") - 1;
			strncpy(chargeTemp.buf, "icid-value=ECE-CLF-ece01-pl01-clf-1550683191255-712;icid-generated-at=ECE-CLF-ece01-pl01-clf-ece01-pl01-clf;orig-ioi=ims.globalstar.com", chargeTemp.pl.l);
            sipTuHdrRawValue_t value = {SIPTU_RAW_VALUE_TYPE_STR_SIPPL, {.sipPLValue = chargeTemp}};
            sipProxyMsgModInfo_addHdr(pMsgModInfo->extraAddHdr, &pMsgModInfo->addNum, SIP_HDR_P_CHARGING_VECTOR, &value);
		}
	}

    if(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE])
    {
        sipProxyMsgModInfo_delHdr(pMsgModInfo->extraDelHdr, &pMsgModInfo->delNum, SIP_HDR_ROUTE, true);
    }

EXIT:
	return status;
} 


//create a odi and store in pSessInfo->tempWorkInfo.lastOdiInfo.odi
//return value: odi
static osPointerLen_t* scscfSess_createOdi(scscfSessInfo_t* pSessInfo)
{
	static uint16_t salt = 0;
	osPointerLen_t* pUser = &pSessInfo->tempWorkInfo.users[0];
	osPointerLen_t* pOdi = &pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl;	
	sipPL_init(&pSessInfo->tempWorkInfo.lastOdiInfo.odi);

	struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    srand(tp.tv_nsec);
    int randValue=rand();

    pOdi->l = snprintf((char*)pOdi->p, SIP_HDR_MAX_SIZE, "%lx%x%d", tp.tv_nsec % tp.tv_sec, randValue, salt++);

    strncpy((char*)&pOdi->p[pOdi->l], pUser->p, pUser->l);
    pOdi->l += pUser->l;
    for(int i=0; i<pOdi->l; i++)
    {
        if((pOdi->p[i] >='0' && pOdi->p[i] <='9') ||(pOdi->p[i] >='A' && pOdi->p[i] <= 'Z') || (pOdi->p[i] >= 'a' && pOdi->p[i] <= 'z'))
        {
            continue;
        }

        //override any other char to be '%'
        ((char*)pOdi->p)[i] = 'a';
    }

	return pOdi;
}


static void scscfSessInfo_workInfoCleanup(scscfSessInfo_t* pSessInfo)
{
	if(!pSessInfo)
	{
		return;
	}

	pSessInfo->tempWorkInfo.pSipTUMsg = osfree(pSessInfo->tempWorkInfo.pSipTUMsg);
	pSessInfo->tempWorkInfo.pReqDecodedRaw = osfree(pSessInfo->tempWorkInfo.pReqDecodedRaw);

	for(int i=0; i< SCSCF_MAX_PAI_NUM; i++)
	{
		pSessInfo->tempWorkInfo.users[i].l = 0;	//if a session contains both MO and MT, user will change
	}
    pSessInfo->tempWorkInfo.lastOdiInfo.pOdiHashElement = NULL;
	pSessInfo->tempWorkInfo.lastOdiInfo.odi.pl.l = 0;
    pSessInfo->tempWorkInfo.pLastIfc = NULL;
}


static void scscfSessInfo_workInfoReset(scscfSessInfo_t* pSessInfo, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pCallId)
{
    if(!pSessInfo)
    {
        return;
    }

	//do we need to free pSipTUMsg and tempWorkInfo.pReqDecodedRaw here?  after forwarding a request to proxy, proxy takes control of pSipTUMsg and pReqDecodedRaw.  Does this module needs to use pSipTUMsg and pReqDecodedRaw. after forwarding a request to proxy? if yes, then, let this module add a reference to these two messages, and do free here.  if not, then, do not need to do free here.
    osfree(pSessInfo->tempWorkInfo.pSipTUMsg);
    osfree(pSessInfo->tempWorkInfo.pReqDecodedRaw);

	pSessInfo->tempWorkInfo.pSipTUMsg = pSipTUMsg;
	pSessInfo->tempWorkInfo.pReqDecodedRaw = pReqDecodedRaw;

	if(pCallId)
	{
		pSessInfo->tempWorkInfo.callId = *pCallId;
	}
}


static void scscfSessInfo_cleanup(void* data)
{
	if(!data)
	{ 
		return;
	}

	scscfSessInfo_t* pSessInfo = data;

	scscfSessInfo_workInfoCleanup(pSessInfo);
	pSessInfo->state = SCSCF_SESS_STATE_INIT;
	osList_delete(&pSessInfo->proxyList);
}
