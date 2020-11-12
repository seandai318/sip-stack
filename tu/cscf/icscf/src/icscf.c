/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxy.c
 ********************************************************/


#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"
#include "sipHdrVia.h"

#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipRegistrar.h"



static osStatus_e proxy_buildSipRsp(sipResponse_e rspCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipTUMsg_t* pSipTUMsg, sipHdrMultiVia_t* pTopMultiVia);

#if 0
static void masRegistrar_cleanup(void* pData);
static osStatus_e proxy_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData);
static void masHashData_cleanup(void* data);
static uint64_t masRegStartTimer(time_t msec, void* pData);
static void proxy_onTimeout(uint64_t timerId, void* data);
static void masRegData_forceDelete(tuRegistrar_t* pRegData);
static osStatus_e proxy_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osStatus_e proxy_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static void sipReg_onRegister(osPointerLen_t* user);
#endif

static osHash_t* proxyHash;
//static osListApply_h appInfoMatchHandler;
//static sipRegAction_t appRegActionData;


osStatus_e proxy_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    proxyHash = osHash_create(bucketSize);
    if(!proxyHash)
    {
        logError("fails to create proxyHash, bucketSize=%u.", bucketSize);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	status = diaConn_initIntf(DIA_INTF_TYPE_CX);

EXIT:
    return status;
}


void sipReg_attach(osListApply_h applyHandler, sipRegAction_t* pRegActionData)
{
    appInfoMatchHandler = applyHandler;

    appRegActionData = *pRegActionData;
}


osStatus_e proxy_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	switch(msgType)
	{
		case SIP_MSG_REQUEST:
			return proxy_onSipMsg(msgType, pSipTUMsg);
			break;
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
			proxy_onTrFailure(msgType, pSipTUMsg);
			break;
		default:
			logError("msgType(%d) is not handled.", msgType);
			break;
	}

	return OS_ERROR_INVALID_VALUE;
}


static osStatus_e proxy_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

	if(msgType != SIP_MSG_REQUEST)
	{
		logError("msgType is not SIP_MSG_REQUEST.");
		return OS_ERROR_INVALID_VALUE;
	}

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		return OS_ERROR_NULL_POINTER;
	}

	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    osPointerLen_t* pContactExpire = NULL;
    tuRegistrar_t* pRegData = NULL;

	//raw parse the whole sip message 
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
    	logError("fails to sipDecodeMsgRawHdr.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //prepare for the extraction of the peer IP/port
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);
    if(status != OS_STATUS_OK)
    {
        logError("fails to decode the top via hdr in sipDecodeHdr.");
        goto EXIT;
    }
	sipHdrViaDecoded_t* pTopVia = ((sipHdrMultiVia_t*)(viaHdr.decodedHdr))->pVia;

	sipHostport_t peerHostPort;
	sipTransport_e peerTpProtocol;
	sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);

	//get the user's IMPU, which is the sip URI in the TO header
	osVPointerLen_t impu = {};
	debug("sean-remove, from-value=%r", &pReqDecodedRaw->msgHdrList[SIP_HDR_TO]->pRawHdr->value);
	status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value, &impu.pl);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipParamUri_getUriFromRawHdrValue for impu.");
		rspCode = SIP_RESPONSE_503;
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    sipHdrDecoded_t authHdr = {};
	osVPointerLen_t impi = {};
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION] != NULL)
	{
	    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION]->pRawHdr, &authHdr, false);
		sipHdrMethodParam_t* pAuthHdrInfo = authHdr.decodedHdr;
		osPointerLen_t username={"username", sizeof("username")-1};
//		osPointerLen_t* sipHdrNameValueList_getValue(sipHdrNameValueList_t* pHdr, sipParamNameValue_t* pNameValue)

		osPointerLen_t* pImpi = sipHdrNameValueList_getValue(&pAuthHdrInfo->nvParamList, &username);
		if(!pImsi)
		{
			logError("no username in the authorization header.");
			rspCode = SIP_RESPONSE_400;
            goto EXIT;
        }

		impi.pl = *pImpi;

		osfree(pAuthHdrInfo);
	}
	else
	{
		//remove sip: or tel: from impu (not support sips), and use the remaining as the impi
		osPL_shiftcpy(&impi.pl, &impu.pl, 4);
	}

	osVPointerLen_t pvni = {};
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_VISITED_NETWORK_ID] != NULL)
	{
	    osPointerLen_t visitedNW={};
		status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_P_VISITED_NETWORK_ID]->pRawHdr, &visitedNW, false);
		sipHdrMultiValueParam_t* pVisitedNW = visitedNW.decodedHdr;
		osPointerLen_t* pPvni = sipHdrMultiValueParam_getValue(pVisitedNW, false);	//the false meaning isBottom, true=isTop
		pvni.pl = *pPvni;
		osfree(pVisitedNW);
	}
	else
	{
		pPvni = proxyConfig_getDefaultPvni();
		pvni.pl = *pPvni;
	}

    //check the expire header
    uint32_t regExpire = 0;
    bool isExpiresFound = false;
    if(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES] != NULL)
    {
        sipHdrDecoded_t expiryHdr;
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES]->pRawHdr, &expiryHdr, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to get expires value from expires hdr by sipDecodeHdr.");
            rspCode = SIP_RESPONSE_400;
            goto EXIT;
        }

        regExpire = *(uint32_t*)expiryHdr.decodedHdr;
        osfree(expiryHdr.decodedHdr);
        isExpiresFound = true;
    }
    else
    {
	    sipHdrDecoded_t contactHdr={};

	    //decode the 1st contact entry
    	logError("sean-remove, before decode SIP_HDR_CONTACT (%d).", SIP_HDR_CONTACT);
    	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, &contactHdr, false);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to decode contact hdr in sipDecodeHdr.");
        	rspCode = SIP_RESPONSE_400;
        	goto EXIT;
    	}

        osPointerLen_t expireName={"expires", 7};
        //pContactExpire is not allocated an new memory, it just refer to a already allocated memory in pGNP->hdrValue, no need to dealloc memory for pContactExpire
        osPointerLen_t* pContactExpire = sipHdrGenericNameParam_getGPValue(&((sipHdrMultiContact_t*)contactHdr.decodedHdr)->contactList.pGNP->hdrValue, &expireName);
        if(pContactExpire != NULL)
        {
            isExpiresFound = true;
            regExpire = osPL_str2u32(pContactExpire);
        }
    }

    if(!isExpiresFound)
    {
        regExpire = SIP_REG_DEFAULT_EXPIRE;
    }

	//now we have enough information to perform a UAR query towards HSS
    DiaCxUarAuthType_e authType = regExpire ? DIA_CX_AUTH_TYPE_REGISTRATION : DIA_CX_AUTH_TYPE_DE_REGISTRATION;
	uint32_t featureList = 1 << DIA_CX_FEATURE_LIST_ID_SIFC;

    diaHdrSessInfo_t diaHdrSessInfo;
    osMBuf_t* pBuf = diaBuildUar(&impi, &impu, &visitedNWId, authType, featureList, NULL, &diaHdrSessInfo);

	status = diaSendAppMsg(pBuf, proxyConfig_getHssAddr());
	osMBuf_dealloc(pBuf);
	if(status != OS_STATUS_OK)
	{
        logError("fails to transport_send for impu(%r)", &impu.pl);
        rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

	//now use diaHdrSessInfo.sessionId to store sip info, to-do, if diaHdrSessInfo.sessionId needs to be freed
    pRegData = oszalloc(sizeof(proxyRegData_t), proxyRegData_cleanup);
	pRegData->pRegHashLE = osHash_addUserData(proxyHash, &diaHdrSessInfo.sessionId, true, pRegData);
	if(!pRegHashData->pRegHashLE)
	{
		logError("fails to osHash_addUserData for impu(%r).", &impu.pl);
		rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	osDPL_dup(&pRegData->impu, &impu->pl);

	logInfo("impu(%r) is add into proxyHash, key=%r, pRegHashLE=%p", &impu.pl, &diaHdrSessInfo.sessionId, pRegData->pRegHashLE);

	pRegData->waitUarResp = osStartTimer(CSCF_WAIT_DIA_RESP_TIME, proxy_onTimeout, pRegData);

EXIT:

	status = proxy_buildSipRsp(rspCode, pReqDecodedRaw, pSipTUMsg, viaHdr.decodedHdr);
	return status;
}


static void proxy_onDiaResponse(diaMsgDecoded_t* pDecoded)
{
	if(!pDecoded)
	{
		logError("null pointer, pDecoded.");
		return;
	}

	if(osList_isEmpty(&pDecoded->avpList))
	{
		logError("pDecoded->avpList is empty.");
		return;
	}

	//the session-id avp must be the first avp
    diaAvp_t* pSessionIdAvp = pDecoded->avpList.head->data;
	if(!pSessionIdAvp || pSessionIdAvp->avpCode != DIA_AVP_CODE_SESSION_ID || pSessionIdAvp->avpData.dataType != DIA_AVP_DATA_TYPE_OCTET_STRING)
	{
		logError("pSessionIdAvp=NULL, or the first avp is not DIA_AVP_CODE_SESSION_ID(%d), or avpData.dataType(%d) is not OCTET_STRING.", pSessionIdAvp ? pSessionIdAvp->avpCode : 0, pSessionIdAvp ?  pSessionIdAvp->avpData.dataType : DIA_AVP_DATA_TYPE_UNKNOWN);
		return;
	} 

	osVPointerLen_t* pSessId = &pSessionIdAvp->avpData.dataStr;
	void* pUserData = osPlHash_getUserData(proxyHash, &pSessId->pl, true);
	if(!pUserData)
	{
		logError("receive a dia message with diaCmdCode=%d, but there is no corresponding hashed data.", pDecoded->cmdCode);
		return;
	}

	switch(pDecoded->cmdCode)
	{
		case DIA_CMD_CODE_UAR:
			mlogInfo(LM_CSCF, "UAA is received.");
			proxyRegData_t* pRegData = pUserData;
			pRegData->waitUarResp = osStopTimer(pRegData->waitUarResp);

			//check the response
			bool isExperimental = false;
			uint32_t resultCode = dia_getResultCode(*pDecoded->avpList, &isExperimental);

			//if error response
			if(resultCode < DIA_RESULT_CODE_OK || resultCode > DIA_RESULT_CODE_OK_MAX)
			{
				mlogInfo(LM_CSCF, "UAA indicts unsuccessful resultCode(%d), isExperimental=%d.", resultCode, isExperimental);	

				sipResponse_e rspCode = cscf_mapDiaErrorCode(resultCode, isExperimental);
				proxy_buildSipRsp(rspCode, pReqDecodedRaw, pSipTUMsg, viaHdr.decodedHdr);
				return;
			}

			struct sockaddr_in destScscf = {};
			status = proxy_processUaa(resultCode, &pDecoded->avpList, pRegData, &destScscf);
			if(status != OS_STATUS_OK)
			{
				logError("fails to proxy_processUaa for diameter resultCode(%d), impu(%r).", resultCode, pRegData->impu);
				proxy_buildSipRsp(500, pReqDecodedRaw, pSipTUMsg, viaHdr.decodedHdr);
				return;
			}

			if(osSA_isSAInvalid(&destScscf))
			{
				status = scscf_onIcscfMsg(DIA_CMD_CODE_UAR, pReqDecodedRaw, pSipTUMsg);
	            if(status != OS_STATUS_OK)
    	        {
        	        logError("fails to scscf_onIcscfMsg for diameter resultCode(%d), impu(%r).", resultCode, pRegData->impu);
            	    proxy_buildSipRsp(500, pReqDecodedRaw, pSipTUMsg, viaHdr.decodedHdr);
                	return;
            	}
			}
			else
			{
				//forward the sip message to the remote SCSCF
				status = proxy_forwardMsg(&destScscf, pReqDecodedRaw, pSipTUMsg);
                if(status != OS_STATUS_OK)
                {
                    logError("fails to proxy_forwardMsg for diameter resultCode(%d), impu(%r).", resultCode, pRegData->impu);
                    proxy_buildSipRsp(500, pReqDecodedRaw, pSipTUMsg, viaHdr.decodedHdr);
                    return;
                }
			}
			break;
		case DIA_CMD_CODE_LIR:
            mlogInfo(LM_CSCF, "LIA is received.");
			break;
		default:
			logError("proxy receives unexpected diaCmdCode(%d).", pDecoded->cmdCode);
			break;
	}
}
 
static osStatus_e proxy_buildSipRsp(sipResponse_e rspCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipTUMsg_t* pSipTUMsg, sipHdrMultiVia_t* pTopMultiVia)
{
	osIpPort_t osPeer;
	osConvertntoPL(pSipTUMsg->pPeer, &osPeer);
    sipHostport_t peer;
	peer.host = osPeer.ip.pl;
	peer.portValue = osPeer.port;

	osMBuf_t* pSipResp = NULL;
    sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
    int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);

	logInfo("rspCode=%d.", rspCode);
	pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
	if(!pSipResp)
	{
		logError("fails to sipTU_buildUasResponse.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    status = sipHdrVia_rspEncode(pSipResp, pTopMultiVia, pReqDecodedRaw, &peer);
	status = sipTU_addContactHdr(pSipResp, pReqDecodedRaw, regExpire);
	status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
	status = sipTU_msgBuildEnd(pSipResp, false);

		logInfo("Response Message=\n%M", pSipResp);

    sipTransMsg_t sipTransMsg = {};

	//fill the peer transport info
   	sipHdrViaDecoded_t* pTopVia = pTopMultiVia->pVia;
   	sipHostport_t peerHostPort;
   	sipTransport_e peerTpProtocol;
   	sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);

    sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
	osIpPort_t ipPort ={{peerHostPort.host}, peerHostPort.portValue};
	osConvertPLton(&ipPort, true, &sipTransMsg.response.sipTrMsgBuf.tpInfo.peer);
	proxyConfig_getHost1(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.response.sipTrMsgBuf.tpInfo.protocolUpdatePos = 0;
	//fill the other info
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
	sipTransMsg.isTpDirect = false;
    sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
    sipTransMsg.pTransId = pSipTUMsg->pTransId;
	sipTransMsg.appType = SIPTU_APP_TYPE_REG;
	sipTransMsg.response.rspCode = rspCode;
	sipTransMsg.pSenderId = pRegData;
    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);
		
EXIT:
    //proxy does not need to keep pSipResp, if other layers need it, it is expected they will ref it
    osfree(pSipResp);
	return status;
}



static osStatus_e proxy_forwardMsg(struct sockaddr_in* pScscf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipTUMsg_t* pSipTUMsg)
{
}


logError("to-remove, viaHdr.decodedHdr=%p, pContactHdr=%p", viaHdr.decodedHdr, pContactHdr);
	osfree(viaHdr.decodedHdr);	
	osfree(pContactHdr);

    osfree(pReqDecodedRaw);

	DEBUG_END
	return status;
}

//make sure dereg purge timer is long enough so that for any possible resp TrFailure error (include TrFailure error for dereg request retransmission), it ALWAYS happens within the purge timer period. (may not have to be, see comment in  pRegData = pSipTUMsg->pTUId assignment. 
static osStatus_e proxy_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//there is some dangerous for this statement, as pRegData may have been reclaimed for dereg case in general.  but actually it may not be an issue, as only possible no call chain call of this function (out of blue) is for UDP TP, but UDP TP by itself would not have out of blue error, either there is error when UDP sending happens, or no error, so we are fine 
	tuRegistrar_t* pRegData = pSipTUMsg->pTUId;
	if(!pRegData)
	{
		logInfo("pRegData is NULL.");
		goto EXIT;
	}

	//only set the flag.  In some case like in TCP case, it may be possible that the TrFailure happens after the reg procedure is completed, but we can not roll back all registration setting, let it be.  For dereg case, make sure the purge timer is longer than the possible TrFailure return time, because otherwise, pRegData maybe a garbage, and that may cause program crash.	but when seeing above  pRegData = pSipTUMsg->pTUId comment, this may not be an issue
	pRegData->isRspFailed = true;

EXIT:
	return status;
}

		
static void proxy_onTimeout(uint64_t timerId, void* data)
{
	logInfo("timeout, timerId=0x%lx.", timerId); 
	if(!data)
	{
		logError("null pointer, data.");
		return;
	}

	tuRegistrar_t* pRegData = data;

	if(timerId == pRegData->expiryTimerId)
	{
		pRegData->expiryTimerId = 0;
		proxy_deRegUser(TU_DEREG_CAUSE_TIMEOUT, pRegData);
	}
	else if(timerId == pRegData->purgeTimerId)
	{
		pRegData->purgeTimerId = 0;
		osfree(pRegData);
	}
	else if(timerId == pRegData->smsQueryTimerId)
	{
        pRegData->smsQueryTimerId = 0;
		if(pRegData->regState == MAS_REGSTATE_REGISTERED)
		{
			//masDbQuerySMSByUser((osPointerLen_t*)&pRegData->user);
			sipReg_onRegister((osPointerLen_t*)&pRegData->user);
		}
	}
	else
	{
		logError("received a unrecognized tiemrId.");
	}

EXIT:
	return;
}


sipUri_t* proxy_getUserRegInfo(osPointerLen_t* pSipUri, tuRegState_e* regState)
{
	sipUri_t* pContactUser = NULL;

    uint32_t key = osHash_getKeyPL(pSipUri, true);
    osListElement_t* pHashLE = osHash_lookupByKey(proxyHash, &key, OSHASHKEY_INT);
    if(!pHashLE)
    {
		debug("user(%r) does not have register record in MAS, key=0x%x.", pSipUri, key);
		*regState = MAS_REGSTATE_NOT_EXIST;
		return pContactUser;
	}

	tuRegistrar_t* pRegData = ((osHashData_t*)pHashLE->data)->pData;
	if(pRegData->regState == MAS_REGSTATE_NOT_REGISTERED)
	{
		debug("user(%r) is not registered.", pSipUri);
        logError("to-remove, regState, pHashLE=%p, pRegData=%p, state=%d, MAS_REGSTATE_REGISTERED=%d", pHashLE, pRegData, pRegData->regState, MAS_REGSTATE_REGISTERED);
		*regState = MAS_REGSTATE_NOT_REGISTERED;
		return pContactUser;
	}

logError("to-remove, masreg, pRegData=%p, peer-ipaddr=%p, peer=%r:%d", pRegData, ((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.host.p, &((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.host, ((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.portValue);
	*regState = MAS_REGSTATE_REGISTERED;
	pContactUser = &((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri;

	return pContactUser;
}
	

void* proxy_addAppInfo(osPointerLen_t* pSipUri, void* pMasInfo)
{
	DEBUG_BEGIN

	if(!pSipUri || !pMasInfo)
	{
		logError("null pointyer, pSipUri=%p, pMasInfo=%p.", pSipUri, pMasInfo);
		return NULL;
	}

	void* pRegId = NULL;

    uint32_t key = osHash_getKeyPL(pSipUri, true);
    osListElement_t* pHashLE = osHash_lookupByKey(proxyHash, &key, OSHASHKEY_INT);
    if(!pHashLE)
    {
        osHashData_t* pHashData = oszalloc(sizeof(osHashData_t), NULL);
        if(!pHashData)
        {
            logError("fails to allocate pHashData.");
            goto EXIT;
        }

        tuRegistrar_t* pRegData = oszalloc(sizeof(tuRegistrar_t), masRegistrar_cleanup);
        osDPL_dup(&pRegData->user, pSipUri);
        pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;
        logError("to-remove, regState, pRegData=%p, state=%d, MAS_REGSTATE_REGISTERED=%d", pRegData, pRegData->regState, MAS_REGSTATE_REGISTERED);

        pHashData->hashKeyType = OSHASHKEY_INT;
        pHashData->hashKeyInt = key;
        pHashData->pData = pRegData;
        pRegData->pRegHashLE = osHash_add(proxyHash, pHashData);
		pRegId = pRegData->pRegHashLE;
	
        pRegData->purgeTimerId = masRegStartTimer(SIP_REG_PURGE_TIMER*1000, pRegData);
    }
	else
	{
		pRegId = pHashLE;
		tuRegistrar_t* pRegData = ((osHashData_t*)pHashLE->data)->pData;
	}

EXIT:	
	DEBUG_END
	return pRegId;
}
	

osStatus_e proxy_deleteAppInfo(void* pRegId, void* pTransId)
{
	if(!pRegId || !pTransId)
	{
		logError("null pointer, pRegId=%p, pTransId=%p.", pRegId, pTransId);
		return OS_ERROR_NULL_POINTER;
	}

	osStatus_e status = OS_STATUS_OK;
	tuRegistrar_t* pRegData = osHash_getData(pRegId);
	if(!pRegData)
	{
		logError("pRegData is NULL.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	debug("pRegData=%p, pTransId=%p", pRegData, pTransId);
	void* pInfo = osList_deleteElement(&pRegData->appInfoList, appInfoMatchHandler, pTransId);
	if(!pInfo)
	{
		logInfo("fails to find appInfo in osList_deleteElement.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//do not free the pInfo, it is owned by masSMS and will be freed there

EXIT:
	return status;
}


static uint64_t masRegStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, proxy_onTimeout, pData);
}


static void sipReg_onRegister(osPointerLen_t* user)
{
	if(appRegActionData.appAction)
	{
		appRegActionData.appAction(user, appRegActionData.pAppData);
	}
}
	

static osStatus_e proxy_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pRegData)
    {
        logError("null pointer, pRegData.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	debug("pRegData->regState = %d", pRegData->regState);
	if(pRegData->regState == MAS_REGSTATE_REGISTERED)
	{
    	pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;

	    if(pRegData->expiryTimerId != 0)
		{
    		osStopTimer(pRegData->expiryTimerId);
			pRegData->expiryTimerId = 0;
		}
		if(pRegData->smsQueryTimerId != 0)
        {
            osStopTimer(pRegData->smsQueryTimerId);
            pRegData->smsQueryTimerId = 0;
        }
    	pRegData->purgeTimerId = masRegStartTimer(SIP_REG_PURGE_TIMER*1000, pRegData);
	}

EXIT:
	return status;
}

	
static void masRegistrar_cleanup(void* pData)
{
	DEBUG_BEGIN;

	if(!pData)
	{
		return;
	}

	tuRegistrar_t* pRegData = pData;

	if(pRegData->expiryTimerId != 0)
    {
        osStopTimer(pRegData->expiryTimerId);
		pRegData->expiryTimerId = 0;
    }

    if(pRegData->smsQueryTimerId != 0)
    {
        osStopTimer(pRegData->smsQueryTimerId);
        pRegData->smsQueryTimerId = 0;
    }

	if(pRegData->purgeTimerId != 0)
	{
		osStopTimer(pRegData->purgeTimerId);
		pRegData->purgeTimerId = 0;
	}

	//to-do, update so that just call one function, osfree(pRegData->pContact) will clean the remaining
//	osfree(pRegData->pContact->decodedHdr);
//	osfree(pRegData->pContact->rawHdr.buf);
    osfree(pRegData->pContact);

	osDPL_dealloc(&pRegData->user);
	osList_delete(&pRegData->appInfoList);

	//remove from hash
	if(pRegData->pRegHashLE)
	{
		logInfo("delete hash element, key=%ud", ((osHashData_t*)pRegData->pRegHashLE->data)->hashKeyInt);
		osHash_deleteNode(pRegData->pRegHashLE, OS_HASH_DEL_NODE_TYPE_KEEP_USER_DATA);
	}

	pRegData->pRegHashLE = NULL;

DEBUG_END
}


static void masRegData_forceDelete(tuRegistrar_t* pRegData)
{
	if(!pRegData)
	{
		return;
	}

    //clear the resource
    if(pRegData->expiryTimerId)
    {
        osStopTimer(pRegData->expiryTimerId);
        pRegData->expiryTimerId = 0;
    }

    if(pRegData->smsQueryTimerId != 0)
    {
        osStopTimer(pRegData->smsQueryTimerId);
        pRegData->smsQueryTimerId = 0;
    }

    if(pRegData->purgeTimerId)
    {
        osStopTimer(pRegData->purgeTimerId);
        pRegData->purgeTimerId = 0;
    }

    osfree(pRegData);
}


static void masHashData_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osHashData_t* pHashData = data;
	osfree(pHashData->pData);
}
