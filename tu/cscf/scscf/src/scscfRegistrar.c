/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfRegistrar.c
 * implement 3GPP 24.229 SCSCF registration. support early-IMS and http digest authentication
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

#include "diaMsg.h"
#include "diaIntf.h"

#include "sipRegistrar.h"
#include "cscfConfig.h"


static void masRegistrar_cleanup(void* pData);
static osStatus_e masReg_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData);
static void masHashData_cleanup(void* data);
static uint64_t masRegStartTimer(time_t msec, void* pData);
static void masReg_onTimeout(uint64_t timerId, void* data);
static void masRegData_forceDelete(tuRegistrar_t* pRegData);
static osStatus_e masReg_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osStatus_e masReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static void sipReg_onRegister(osPointerLen_t* user);


static osStatus_e scscReg_sendResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t regExpire, struct sockaddr_in* pPeer, struct sockaddr_in* pLocal, sipResponse_e rspCode);
static osStatus_e scscfReg_decodeSaa(osPointerLen_t* pImpu, diaMsgDecoded_t* pDiaDecoded, scscfReg_userProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo);
static osStatus_e scscfReg_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pHashData, diaResultCode_t* pResultCode);
static inline osPointerLen_t* scscfReg_getUserId(scscfRegIdentity_t* pIdentity);
static osStatus_e scscfReg_performRegSar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, pRegData, dia3gppServerAssignmentType_e saType);
static void scscfReg_createSubHash(scscfRegInfo_t* pRegInfo);
static void scscfReg_deleteSubHash(scscfRegInfo_t pRegInfo);


static osHash_t* scscfRegHash;
static osListApply_h appInfoMatchHandler;
static sipRegAction_t appRegActionData;






osStatus_e scscfReg_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    masRegHash = osHash_create(bucketSize);
    if(!scscfRegHash)
    {
        logError("fails to create scscfRegHash, bucketSize=%u.", bucketSize);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


void sipReg_attach(osListApply_h applyHandler, sipRegAction_t* pRegActionData)
{
    appInfoMatchHandler = applyHandler;

    appRegActionData = *pRegActionData;
}


osStatus_e masReg_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	switch(msgType)
	{
		case SIP_MSG_REQUEST:
			return masReg_onSipMsg(msgType, pSipTUMsg);
			break;
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
			masReg_onTrFailure(msgType, pSipTUMsg);
			break;
		default:
			logError("msgType(%d) is not handled.", msgType);
			break;
	}

	return OS_ERROR_INVALID_VALUE;
}


static void scscfReg_onSipReg(sipTUMsg_t* pSipTUMsg)
{
    if(!pSipTUMsg)
    {
        logError("null pointer, pSipTUMsg.");
        return;
    }

    sipResponse_e rspCode = SIP_RESPONSE_INVALID;

	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
        logError("fails to sipDecodeMsgRawHdr.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

    //prepare for the extraction of the peer IP/port
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);
    if(status != OS_STATUS_OK)
    {
        logError("fails to decode the top via hdr in sipDecodeHdr.");
		rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

	osPointerLen_t sipUri;
    if(sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value, &sipUri) != OS_STATUS_OK)
    {
        logError("fails to sipParamUri_getUriFromRawHdrValue.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

	osPointerLen_t impi;
	if(cscf_getImpiFromSipMsg(pReqDecodedRaw, &sipUri, &impi) != OS_STATUS_OK)
	{
		logError("fails to cscf_getImpiFromSipMsg.")
		rspCode = SIP_RESPONSE_400;
		goto EXIT;
	}

	scscfReg_processRegMsg(&impi, &sipUri, pSipTUMsg, pReqDecodedRaw, NULL);

EXIT:
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		scscReg_sendResponse(pReqDecodedRaw, 0, pSipTUMsg->pPeer, NULL, rspCode);
	}

	return;
}



void scscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, struct sockaddr_in* pLocalHost)
{
    osStatus_e status = OS_STATUS_OK;

    sipHdrDecoded_t* pContactHdr = NULL;oszalloc(sizeof(sipHdrDecoded_t), sipHdrDecoded_cleanup);
    sipHdrDecoded_t viaHdr={};
	
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    osPointerLen_t* pContactExpire = NULL;
    scscfRegInfo_t* pRegInfo = NULL;

	//decode the 1st contact entry
	logError("sean-remove, before decode SIP_HDR_CONTACT (%d).", SIP_HDR_CONTACT);
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, pContactHdr, true);
    if(status != OS_STATUS_OK)
    {
        logError("fails to decode contact hdr in sipDecodeHdr.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

    //check the expire header
    uint32_t regExpire = 0;
	rspCode = scscf_getRegExpireFromMsg(pReqDecodedRaw, &regExpire, &pContactHdr);
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		goto EXIT;
	}

	pRegInfo = osPlHash_getUserData(scscfRegHash, pImpi, true);
    if(!pRegInfo)
    {
		if(regExpire == 0)
		{
			logInfo("received a out of blue deregister message, impi=%r", pImpi);
			rspCode = SIP_RESPONSE_200;
			goto EXIT;
		}

		pRegInfo = oszalloc(sizeof(tuRegistrar_t), scscfRegInfo_cleanup);
        osDPL_dup(&pRegInfo->user, pImpi);
		pLocalHost ? pRegInfo->regMsgInfo.sipLocalHost = *pLocalHost : (void)0;
		pRegInfo->pRegHashLE = osPlHash_addUserData(scscfRegHash, pImpi, true, pRegInfo);
		if(!pRegInfo->pRegHashLE)
		{
			logError("fails to osPlHash_addUserData for impi(%r).", pImpi);
            rspCode = SIP_RESPONSE_500;
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
		
		logInfo("user(%r) is add into masRegHash, key=0x%x, pRegHashLE=%p", pImpi, key, pRegData->pRegHashLE);
        logError("to-remove, regState, pRegHashLE=%p, pRegData=%p, state=%d, MAS_REGSTATE_REGISTERED=%d", pRegData->pRegHashLE, pRegData, pRegData->regState, MAS_REGSTATE_REGISTERED);

		pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;				
	}

	if(SCSCF_IS_AUTH_ENABLED)
	{
		if(pRegData->regState != MAS_REGSTATE_REGISTERED ||(pRegData->regState == MAS_REGSTATE_REGISTERED && SCSCF_IS_REREG_PERFORM_AUTH))
		{
			if(scscfReg_performMar() != OS_STATUS_OK)
    		{
        		mlogInfo(LM_CSCF, "fails to scscfReg_performSar for impu(%r).", pSipUri);
        		rspCode = SIP_RESPONSE_500;
    		}
			goto EXIT;
		}
	}

	
	if(scscfReg_isPerformSar(pRegData, regExpire))
	{
		if(scscfReg_performRegSar(pImpi, pImpu, pRegData) != OS_STATUS_OK)
		{
			mlogInfo(LM_CSCF, "fails to scscfReg_performSar for impu(%r).", pSipUri);
			rspCode = SIP_RESPONSE_500;
		}
	}
	else
	{
		//perform 3rd party registration.  if SAR is performed, this function will be called when SAA returns
		scscfReg_perform3rdPartyReg(pRegData);
	}
		
EXIT:
    if(rspCode != SIP_RESPONSE_INVALID)
    {
        scscReg_sendResponse(pReqDecodedRaw, 0, pSipTUMsg->pPeer, pLocalHost, rspCode);
    }

	return;
}


static osStatus_e scscfReg_performRegSar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, pRegData)
{
	osStatus_e status = OS_STATUS_OK;

    dia3gppServerAssignmentType_e saType;
    switch(pRegData->regState)
    {
        case MAS_REGSTATE_REGISTERED:
            if(regExpire == 0)
            {
                saType = DIA_3GPP_CX_USER_DEREGISTRATION;
            }
            else
            {
                saType = DIA_3GPP_CX_RE_REGISTRATION;
            }
            break;
        case MAS_REGSTATE_NOT_REGISTERED:
        case MAS_REGSTATE_UN_REGISTERED:
            if(regExpire == 0)
            {
                //simple accept
                rspCode = SIP_RESPONSE_200;
            }
            else
            {
                saType = DIA_3GPP_CX_REGISTRATION;
            }
            break;
        default:
            logError("received sip REGISTER with expire=%d when pRegData->regState=%d, this shall never happen, reject.", regExpire, pRegData->regState);
            status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
            break;
    }

    status = diaCx_initSAR(pImpi, pImpu, saType, scscfReg_onDiaMsg, pRegData->pRegHashLE);
    if(status != OS_STATUS_OK)
    {
        logError("fails to diaCx_initSAR for impu(%r) for DIA_3GPP_CX_UNREGISTERED_USER.", pImpu);
        goto EXIT;
    }

EXIT:
    return status;
}


static osStatus_e scscfReg_perform3rdPartyReg(scscfRegInfo_t* pRegData, scscfIfcEvent_t* pIfcEvent, bool* isDone)
{
	osStatus+e status = OS_STATUS_OK;
	*isDone = false;
	if(!pRegData || osList_isEmpty(&pRegData->ueList))
	{
		logError("pRegData(%p) is null or ueList is empty.", pRegData);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    osPointerLen_t* pAs = scscfIfc_getNextAS(&pRegData->tempWorkInfo.pLastIfc, pRegData->tempWorkInfo.pReqDecodedRaw, pIfcEvent);
    if(!pAs)
    {
        *isDone = true;
        goto EXIT;
    }

    osPointerLen_t* pImpu = scscfReg_getNoBarImpu(&pRegData->ueList, true); //true=tel uri is preferred
    if(!pImpu)
    {
        logError("no no-barring impu is available.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	osVPointerLen_t asUri = {};
	bool isExistPort = false;
	sipRawUri_getUri(pAs, &asUri.pl, &isExistPort);

#if 0
//create a UAC request with req line, via, from, to, callId and max forward.  Other headers needs to be added by user as needed
//be noted this function does not include the extra "\r\n" at the last of header, user needs to add it when completing the creation of a SIP message
osMBuf_t* sipTU_uacBuildRequest(sipRequest_e code, sipUri_t* pReqlineUri, osPointerLen_t* called, osPointerLen_t* caller, sipTransViaInfo_t* pTransViaId, size_t* pViaProtocolPos)
#endif
#if 0	
    size_t topViaProtocolPos = 0;
    sipTransInfo_t sipTransInfo;

    osMBuf_t* pSipBuf = sipTU_uacBuildRequest(SIP_METHOD_REGISTER, pAs, SCSCF_URI_WITH_PORT, pImpu, &sipTransInfo.transId.viaId, &topViaProtocolPos);
    if(!pSipBuf)
    {
        logError("fails to sipTU_uacBuildRequest for a impu(%r) towards AS(%r).", pImpu, pAs);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    //now add other headers. add cSeq, p-asserted-id, content-type
	sipPointerLen_t cSeqHdr = SIPPL_INIT(cSeqHdr);
	cSeqHdr.pl.l = osPrintf_buffer(cSeqHdr.pl.p, SIP_HDR_MAX_SIZE, "%d REGISTER", sipHdrCSeq_generateValue());	
    status = sipMsgAppendHdrStr(pSipBuf, "CSeq", &cSeqHdr.pl, 0);

//    status = sipMsgAppendHdrStr(pSipBuf, "P-Asserted-Identity", caller, 0);

//    osPointerLen_t cType = {"message/cpim", 12};
//    status = sipMsgAppendHdrStr(pSipBuf, "Content-Type", &cType, 0);
#else
    void* pTransId = NULL;
	int len = 0;
    sipProxy_msgModInfo_t msgModInfo = {false, 0};
	msgModInfo.isChangeCallId = true;

	//add to
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_TO, pImpu);

	//add from
	sipPointerLen_t fromHdr = SIPPL_INIT(fromHdr);
	sipPointerLen_t tagId = SIPPL_INIT(tagId);
	sipHdrFromto_generateSipPLTagId(&tagId, true);
	len = osPrintf_buffer(fromHdr.pl.p, SIP_HDR_MAX_SIZE, "%s;%r", SCSCF_URI_WITH_PORT, &tagId->pl);
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_FROM, &fromHdr->pl);

	//add route
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_ROUTE, pAs);

	//add contact
	osPointerLen_t scscfContact = SCSCF_URI_WITH_PORT;
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_CONTACT, &scscfContact);

	//add P-Charging-Function-Addresses
	sipPointerLen_t chgInfo = SIPPL_INIT(chgInfo);
	if(pRegInfo->hssChgInfo.chgAddrType != CHG_ADDR_TYPE_INVALID)
	{
		len = osPrintf_buffer(chgInfo.pl.p, SIP_HDR_MAX_SIZE, "%s=\"%r\"", pRegInfo->hssChgInfo.chgAddrType == CHG_ADDR_TYPE_CCF ? "ccf" : "ecf", &pRegInfo->hssChgInfo.chgAddr.pl);
		if(len <0)
		{
			logError("fails to osPrintf_buffer for P-Charging-Function-Addresses, ignore this header.");
        }
		else
		{
        	chgInfo.pl.l = len;
		}

		sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES, &chgInfo.pl);		
	}

	//delete hdrs
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_TO, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_FROM, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_ROUTE, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_REQUIRE, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_SUPPORTED, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_PATH, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_CONTACT, true);

//to-do, fill pProxyInfo, 3rdparty reg shall continue when receiving response from AS
    status = sipProxy_forwardReq(pSipTUMsg, pReqDecodedRaw, pTargetUri, &msgModInfo, &nextHop, false, pCallInfo->pProxyInfo, &pTransId);
    if(status != OS_STATUS_OK || !pTransId)
    {
        logError("fails to forward sip request, status=%d, pTransId=%p.", status, pTransId);
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }
#endif

EXIT:
	return status;
}	
	
	

//make sure dereg purge timer is long enough so that for any possible resp TrFailure error (include TrFailure error for dereg request retransmission), it ALWAYS happens within the purge timer period. (may not have to be, see comment in  pRegData = pSipTUMsg->pTUId assignment. 
static osStatus_e masReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
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

		
static void masReg_onTimeout(uint64_t timerId, void* data)
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
		masReg_deRegUser(TU_DEREG_CAUSE_TIMEOUT, pRegData);
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


sipUri_t* masReg_getUserRegInfo(osPointerLen_t* pSipUri, tuRegState_e* regState)
{
	sipUri_t* pContactUser = NULL;

    uint32_t key = osHash_getKeyPL(pSipUri, true);
    osListElement_t* pHashLE = osHash_lookupByKey(masRegHash, &key, OSHASHKEY_INT);
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
	



static uint64_t masRegStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masReg_onTimeout, pData);
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



static void masHashData_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osHashData_t* pHashData = data;
	osfree(pHashData->pData);
}


//note, when sessInitSAR, the hash key will be a impu.  when SAR is triggered by a register, hash key shall use mpui, to avoid the collision.
osStatus_e scscfReg_sessInitSAR(osPointerLen_t* pImpu, scscfHssNotify_h scscfSess_onHssMsg, void* pSessData)
{
    ScscfSessInfo_t* pSessInfo = osmalloc(sizeof(ScscfSessInfo_t), NULL);
    pSessInfo->scscfHssNotify = scscfSess_onHssMsg;
    pSessInfo->sessData = pSessData;

	scscfRegInfo_t* pHashData = osPlHash_getUserData(scscfRegHash, pImpu, true);
	if(!pHashData)
	{
		pHashData = oszalloc(sizeof(scscfRegInfo_t), scscfRegHashData_cleanup);
		pHashLE = pHashLE;
		osList_append(&pHashData->sessDatalist, pSessInfo);

		osListElement_t* pHashLE = osHashPl_addUserData(scscfRegHash, pImpu, true, pHashData);
        pHashData->pHashLE = pHashLE;
        pHashData->state = SCSCF_REG_STATE_UN_REGISTERED;
	}

	status = diaCx_initSAR(NULL, pImpu, DIA_3GPP_CX_UNREGISTERED_USER, scscfReg_onDiaMsg, pHashLE);
	if(status != OS_STATUS_OK)
	{
		logError("fails to diaCx_initSAR for impu(%r) for DIA_3GPP_CX_UNREGISTERED_USER.", pImpu);
		goto EXIT;
	}
	else
	{
		osList_append(&pHashData->sessDatalist, pSessInfo);
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		osHash_deleteNode(pHashLE,OS_HASH_DEL_NODE_TYPE_ALL);
	}
	return status;
}


//pDiaDecoded = NULL indicates no diameter response is received, like timed out
//need to save HSS msg, to-do
void scscfReg_onDiaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData)
{
	if(!pAppData)
	{
		logError("null pointer, pAppData.");
		goto EXIT;
	}

	scscfRegInfo_t* pRegInfo = osHash_getUserDataByLE(pAppData);
	if(!pRegInfo)
	{
		logError("pRegInfo is NULL for hashLE(%p), this shall never happen.", pAppData);
		goto EXIT;
	}

	diaResultCode_t resultCode;
	status = scscfReg_decodeHssMsg(pDiaDecoded, pRegInfo, &resultCode);
	if(status != OS_STATUS_OK)
	{
		logError("fails to scscfReg_decodeHssMsg for pHashData(%p).", pHashData);
		//to-do, needs to free hash, and notify sessions
		goto EXIT;
	}

	switch(pDiaDecoded->cmdCode)
	{
		case DIA_CMD_CODE_SAR:
			scscfReg_onSaa(pAppData, resultCode);
			break;
        case DIA_CMD_CODE_MAR:
			scscfReg_onMaa(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_PPR:
            scscfReg_onPpr(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_RTR:
            scscfReg_onRtr(pRegInfo, resultCode);
			break;
		default:
			logError("scscf receives dia cmdCode(%d) that is not supported, ignore.", pDiaDecoded->cmdCode);
			break;
	}
			
EXIT:
	return;	
}


osStatus_e scscfReg_onSaa(osListElement_t* pUserHashLE, diaResultCode_t resultCode)
{
    scscfRegInfo_t* pRegInfo = osHash_getUserDataByLE(pUserHashLE);

	switch(pRegInfo->sarRegType)
	{
		case if(SCSCF_REG_SAR_UN_REGISTER)
  		{ 
    		osListElement_t* pLE = pRegInfo->sessDatalist.head;
        	if(!pLE)
        	{
        		logError("no session waits on HSS response, it shall not happen, pHashData=%p.", pRegInfo);
        	}
        	else
        	{
        		while(pLE)
            	{
            		ScscfSessInfo_t* pSessInfo = pLE->data;
                	pSessInfo->scscfHssNotify(pSessInfo->sessData, DIA_CMD_CODE_SAR, resultCode, &pRegInfo->regInfo.userProfile);
                	pLE = pLE->next;
            	}

            	osList_clear(&pHashData->sessDatalist);
			}
			
			if(!dia_is2xxxResultCode(resultCode))
			{
            	osHash_deleteNode(pUserHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
			}
			else
			{
				pRegInfo->regState = SCSCF_REG_STATE_UN_REGISTERED;
            	pRegInfo->purgeTimerId = osStartTimer(SCSCFREG_USER_PURGE_TIME, scscfReg_onTimeout, pRegInfo);
        	}
			break;
    	}
		case SCSCF_REG_SAR_REGISTER:
		{
		    sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
	        scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

			if(rspCode < 200 || rspCode >= 300)
			{
				osHash_deleteNode(pUserHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
			}
			else
			{
				//create hash for all identities belong to the UE's subscription
				scscfReg_createSubHash(pRegInfo);

		       //continue 3rd party registration
				pRegInfo->regState = SCSCF_REG_STATE_REGISTERED;
				pRegInfo->expiryTimerId = osStartTimer(pRegInfo->ueContactInfo.regExpire, scscfReg_onTimeout, pRegInfo); 
    		    scscfReg_perform3rdPartyReg(pRegInfo);
			}
			break;
		}
		case SCSCF_REG_SAR_RE_REGISTER:
        {
            sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
            scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

            if(rspCode > 199 && rspCode < 299)
            {
               //continue 3rd party registration
                pRegInfo->expiryTimerId = osRestartTimer(pRegInfo->expiryTimerId);
                scscfReg_perform3rdPartyReg(pRegInfo);
            }
            break;
        }
		case SCSCF_REG_SAR_DE_REGISTER:
		{
            sipResponse_e rspCode = scscf_hss2SipRspCodeMap(resultCode);
            scscReg_sendResponse(pRegInfo->regMsgInfo.pReqDecodedRaw, 0, pRegInfo->regMsgInfo.pSipTUMsg->pPeer, osSA_isInvalid(pRegInfo->regMsgInfo.sipLocalHost)? NULL : &pRegInfo->regMsgInfo.sipLocalHost, rspCode);

			//no need to perform 3rd party deregistration here as it is done in parallel to performing SAR
			scscfReg_deleteSubHash(pRegInfo);
			break;
		}
		default:
			logError("a UE receives a SAA while in sarRegType=%d, this shall never happen.", pRegInfo->sarRegType);
			break;
	}

EXIT:
	return;
}


static osStatus_e scscfReg_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode)
{
	osStatus_e status = OS_STATUS_OK;

	switch(pDiaDecoded->cmdCode)
	{
		case DIA_CMD_CODE_SAR:
			if(!(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST))
			{
				logError("received SAR request, ignore.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			status = scscfReg_decodeSaa(pImpu, pDiaDecoded, &pRegInfo->usrProfile, pResultCode, &pHashData->chgInfo);
			break;
		default:
			logError("unexpected dia command(%d) received, ignore.", pDiaDecoded->cmdCode);
			break;
	}

EXIT:
	return status;
}



static osStatus_e scscfReg_decodeSaa(diaMsgDecoded_t* pDiaDecoded, scscfReg_userProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo)
{
	osStatus_e status = OS_STATUS_OK;

	//starts from avp after sessionId
	osListElement_t* pLE = pDiaDecoded->avpList.head->next;
	while(pLE)
	{
		diaAvp_t* pAvp = pLE->data;
		switch(pAvp->avpCode)
		{
			case DIA_AVP_CODE_RESULT_CODE:
				isResultCode = true;
				pResultCode->resultCode = pAvp->avpData.data32;
				goto EXIT;
				break;
			case DIA_AVP_CODE_EXPERIMENTAL_RESULT_CODE:
				isResultCode = false;
				pResultCode->expCode = pAvp->avpData.data32;	
				goto EXIT;
				break;
			case DIA_AVP_CODE_CX_USER_DATA_CX:
				osVPointerLen_t* pXmlUserData = &pAvp->avpData.dataStr;
				status = scfConfig_parseUserProfile(pXmlUserData, pUsrProfile);
				break;
			case DIA_AVP_CODE_CX_CHARGING_INFO:
			{
				osList_t* pChgInfoList = pAvp->avpData.dataGrouped.dataList;
				if(pChgInfoList)
				{
					osListElement_t* pLE = pChgInfoList->head;
					while(pLE)
					{
						diaAvp_t* pAvp = pLE->data;
						if(pAvp)
						{
							if(pAvp->avpCode == DIA_AVP_CODE_CX_PRI_CHG_COLLECTION_FUNC_NAME)
							{
								pChgInfo.chgAddrType = CHG_ADDR_TYPE_CCF;
								osVPL_copyPL(&pChgInfo.chgAddr, &pAvp->avpData.dataStr);
							}
							else if(pAvp->avpCode == DIA_AVP_CODE_CX_PRI_EVENT_CHG_FUNC_NAME)
							{
								pChgInfo.chgAddrType = CHG_ADDR_TYPE_ECF;
								osVPL_copyPL(&pChgInfo.chgAddr, &pAvp->avpData.dataStr);
							}
							else
							{
								mlogInfo(LM_CSCF, "pAvp->avpCode(%d) is ignored.", pAvp->avpCode);
							}
						}
						pLE = pLE->next;
					}
				}
				break;
			}
			case DIA_AVP_CODE_USER_NAME:
				*pUserName = pAvp->avpData.dataStr;
				break;
			default:
				mlogInfo(LM_CSCF, "avpCode(%d) is not processed.", pAvp->avpCode);
				break;
		}

		pLE = pLE->next;
	}

EXIT:
	return status;
}


static osStatus_e scscReg_sendResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, scscfRegInfo_t* pRegInfo, uint32_t regExpire, struct sockaddr_in* pLocal, sipResponse_e rspCode)
{
	osStatus_e status = OS_STATUS_OK;

	osIpPort_t osPeer;
	osConvertntoPL(pRegInfo->regMsgInfo.pSipTUMsg->pPeer, &osPeer);
    sipHostport_t peer;
    peer.host = osPeer.ip.pl;
    peer.portValue = osPeer.port;

    osMBuf_t* pSipResp = NULL;
    sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
    int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);

    logInfo("rspCode=%d.", rspCode);
    switch(rspCode)
    {
        case SIP_RESPONSE_200:
            pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
            status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
            status = sipTU_addContactHdr(pSipResp, pReqDecodedRaw, regExpire);
            status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
			//add Service-Route
			sipPointerLen_t sr = SIPPL_INIT(sr);
			int len = osPrintf_buffer(sr.pl.p, SIP_HDR_MAX_SIZE, "Service-Route: <sip: %r:%d; orig; lr>\r\n", SCSCF_URI, SCSCF_LISTENING_PORT);
			if(len < 0)
			{
				logError("fails to osPrintf_buffer for service-route.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			} 
			sr.pl.l = len;

            osMBuf_writePL(pSipResp, &sr.pl, true);

			//add P-Associated-URI
			if(pRegInfo)
			{
				sipPointerLen_t pau = SIPPL_INIT(pau);
				osListElement_t* pLE = pRegInfo->ueList.head;
				while(pLE)
				{
					scscfRegIdentity_t* pId = pLE->data;
					if(!pId->isImpi)
					{
						if(!pId->impuInfo.isBarred)
						{
							int len = osPrintf_buffer(pau.pl.p, SIP_HDR_MAX_SIZE, "P-Associated-URI: <%r>\r\n", &pId->impuInfo.impu);
				            if(len < 0)
            				{
                				logError("fails to osPrintf_buffer for service-route.");
                				status = OS_ERROR_INVALID_VALUE;
                				goto EXIT;
            				}
							pau.pl.l = len;

							osMBuf_writePL(pSipResp, &pau.pl, true);
						}
					}
							
					pLE = pLE->next;
				}
			}

			//add P-Charging-Function-Addresses if exists
			if(pRegInfo)
			{
				sipPointerLen_t chgInfo = SIPPL_INIT(chgInfo);
				if(pRegInfo->hssChgInfo.chgAddrType != CHG_ADDR_TYPE_INVALID)
				{
					len = osPrintf_buffer(chgInfo.pl.p, SIP_HDR_MAX_SIZE, "P-Charging-Function-Addresses: %s=\"%r\"\r\n", pRegInfo->hssChgInfo.chgAddrType == CHG_ADDR_TYPE_CCF ? "ccf" : "ecf", &pRegInfo->hssChgInfo.chgAddr.pl);
					if(len <0)
					{
						logError("fails to osPrintf_buffer for P-Charging-Function-Addresses.");
                        status = OS_ERROR_INVALID_VALUE;
                        goto EXIT;
                    }
					chgInfo.pl.l = len;
				}
			}
	
            status = sipTU_msgBuildEnd(pSipResp, false);
            break;
        case SIP_RESPONSE_INVALID:
            //do nothing here, since pSipResp=NULL, the implementation will be notified to abort the transaction
			goto EXIT;
            break;
        default:
            pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
            status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
            if(rspCode == SIP_RESPONSE_423)
            {
                status = sipTU_addMsgHdr(pSipResp, SIP_HDR_MIN_EXPIRES, &regExpire, NULL);
            }
            status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
            status = sipTU_msgBuildEnd(pSipResp, false);
            break;
    }

	mlogInfo(LM_CSCF, "Response Message=\n%M", pSipResp);

    sipTransMsg_t sipTransMsg = {};

    //fill the peer transport info
    sipHdrViaDecoded_t* pTopVia = ((sipHdrMultiVia_t*)(viaHdr.decodedHdr))->pVia;
    sipHostport_t peerHostPort;
    sipTransport_e peerTpProtocol;
    sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);

    sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
    osIpPort_t ipPort ={{peerHostPort.host}, peerHostPort.portValue};
    osConvertPLton(&ipPort, true, &sipTransMsg.response.sipTrMsgBuf.tpInfo.peer);
	if(pSipLocalHost)
	{
		sipTransMsg.response.sipTrMsgBuf.tpInfo.local = *pSipLocalHost;
	}
	else
	{
	    sipConfig_getHost1(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local);
	}
		
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

    //scscfRegistrar does not need to keep pSipResp, if other layers need it, it is expected they will ref it
    osfree(pSipResp);

EXIT:
logError("to-remove, viaHdr.decodedHdr=%p, pContactHdr=%p", viaHdr.decodedHdr, pContactHdr);
    osfree(viaHdr.decodedHdr);
    osfree(pContactHdr);

    osfree(pReqDecodedRaw);
}	


static void scscfReg_createSubHash(scscfRegInfo_t* pRegInfo)
{
    //go through all ID list, and add one at a time.
    osListElement_t* pLE = pRegInfo->ueList.head;
    while(pLE)
    {
		scscfRegIdentity_t* pRegIdentity = pLE->data;
		osPointerLen_t* pId = pRegIdentity->isImpi ? &pRegIdentity->impi : &pRegIdentity->impuInfo.impu;
		osListElement_t* pHashLE = osPlHash_getElement(scscfRegHash, pId, true);
		if(pHashLE)
		{
			pOldRegInfo = osHash_replaceUserData(scscfRegHash, pHashLE, osmemref(pRegInfo));
			osfree(pOldRegInfo);
		}
		else
		{
			pHashLE = osPlHash_addUserData(scscfRegHash, pId, true, osmemref(pRegInfo));
		}
        pRegIdentity->pRegHashLE = pHashLE;

		pLE = pLE->next;
	}
}
	
	
//delete hash data for the whole subscription of a UE
static void scscfReg_deleteSubHash(scscfRegInfo_t* pRegInfo)
{
	//go through all ID list, and delete them.
	osListElement_t* pLE = pRegInfo->ueList.head;
	while(pLE)
	{	
    	osHash_deleteNode(((scscfRegIdentity_t*)pLE->data)->pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
		pLE = pLE->next;
	}
}


static inline osPointerLen_t* scscfReg_getUserId(scscfRegIdentity_t* pIdentity)
{
	if(!pIdentity)
	{
		return NULL;
	}

	if(pIdentity->isImpi)
	{
		return &pIdentity->impi;
    }
    else
    {
    	return &pIdentity->impuInfo.impu;
    }
}
