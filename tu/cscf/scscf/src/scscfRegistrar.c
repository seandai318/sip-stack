/********************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfRegistrar.c
 * implement 3GPP 24.229 SCSCF registration. support early-IMS and http digest authentication
 ********************************************************************8***********************/


#include "stdlib.h"

#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"
#include "osPrintf.h"

#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"
#include "sipHdrVia.h"
#include "sipHdrFromto.h"
#include "sipMsgFirstLine.h"

#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTUMisc.h"
#include "proxyHelper.h"

#include "diaMsg.h"
#include "diaIntf.h"
#include "diaCxSar.h"

#include "scscfRegistrar.h"
#include "cscfConfig.h"
#include "scscfCx.h"
#include "cscfHelper.h"
#include "scscfIfc.h"
#include "scscfIntf.h"



static void scscfReg_onSipRequest(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static void scscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isViaIcscf);
static osStatus_e scscfReg_forwardSipRegister(scscfRegInfo_t* pRegInfo, sipTuAddr_t* pNextHop);
static void scscfReg_dnsCallback(dnsResResponse_t* pRR, void* pData);
static osStatus_e scscfReg_onSipResponse(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osStatus_e scscfReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osPointerLen_t* scscfReg_getNoBarImpu(osList_t* pUeList, bool isTelPreferred);

static bool scscfReg_performNWDeRegister(scscfRegInfo_t* pRegInfo);
static osStatus_e scscfReg_createAndSendSipDeRegister(scscfRegInfo_t* pRegInfo, scscfAsRegInfo_t* pAsInfo);

static void scscfRegInfo_cleanup(void* data);

static inline osPointerLen_t* scscfReg_getUserId(scscfRegIdentity_t* pIdentity);
static inline bool scscfReg_isPerformMar(scscfRegState_e regState, bool isAuthForReReg);
static inline bool scscfReg_isPeformSar(scscfRegState_e regState, int regExpire);

static uint64_t scscfReg_startTimer(time_t msec, scscfRegInfo_t* pRegInfo);



static osHash_t* gScscfRegHash;



osStatus_e scscfReg_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    gScscfRegHash = osHash_create(bucketSize);
    if(!gScscfRegHash)
    {
        logError("fails to create gScscfRegHash, bucketSize=%u.", bucketSize);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    char configDir[80];
    if(snprintf(configDir, 80, "%s%s", getenv("HOME"), CSCF_CONFIG_FOLDER) >= 80)
    {
        logError("the size of config directory is larger than 80.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	status = scscfIfc_init(configDir, SCSCF_SIFC_XSD_FILE_NAME, SCSCF_SIFC_XML_FILE_NAME);

    sipTU_attach(SIPTU_APP_TYPE_SCSCF_REG, scscfReg_onTUMsg);

EXIT:
    return status;
}


//the entering point for scscfReg to receive SIP messages from the transaction layer
osStatus_e scscfReg_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	switch(msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			if(pSipTUMsg->sipMsgType == SIP_MSG_REQUEST)
			{
				scscfReg_onSipRequest(msgType, pSipTUMsg);
				return OS_STATUS_OK;
			}
			else if(pSipTUMsg->sipMsgType == SIP_MSG_RESPONSE)
			{
				return scscfReg_onSipResponse(msgType, pSipTUMsg);
			}
			else
			{
				logError("received unexpected pSipTUMsg->sipMsgType(%d).", pSipTUMsg->sipMsgType);
				return OS_ERROR_INVALID_VALUE;
			}
			break; 
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
		case SIP_TU_MSG_TYPE_TRANSPORT_ERROR:
        case SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE:
			scscfReg_onTrFailure(msgType, pSipTUMsg);
			break;
		default:
			logError("msgType(%d) is not handled.", msgType);
			break;
	}

	return OS_ERROR_INVALID_VALUE;
}


#if 0
//note, when sessInitSAR, the hash key will be a impu.  when SAR is triggered by a register, hash key shall use mpui, to avoid the collision.
osStatus_e scscfReg_sessInitSAR(osPointerLen_t* pImpu, scscfHssNotify_h scscfSess_onHssMsg, void* pSessData)
{
    ScscfSessInfo_t* pSessInfo = osmalloc(sizeof(ScscfSessInfo_t), NULL);
    pSessInfo->scscfHssNotify = scscfSess_onHssMsg;
    pSessInfo->sessData = pSessData;

    scscfRegInfo_t* pHashData = osPlHash_getUserData(gScscfRegHash, pImpu, true);
    if(!pHashData)
    {
        pHashData = oszalloc(sizeof(scscfRegInfo_t), gScscfRegHashData_cleanup);
        pHashLE = pHashLE;
        osList_append(&pHashData->sessDatalist, pSessInfo);

        osListElement_t* pHashLE = osHashPl_addUserData(gScscfRegHash, pImpu, true, pHashData);
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
#endif

//the entering point for scscfReg to receive SIP messages from icscf
osStatus_e scscfReg_onIcscfMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpi, osPointerLen_t* pImpu)
{
	if(!pSipTUMsg || !pReqDecodedRaw || !pImpi || !pImpu)
	{
		logError("NULL pointer, pSipTUMsg=%p, pReqDecodedRaw=%p, pImpi=%p, pImpu=%p.", pSipTUMsg, pReqDecodedRaw, pImpi, pImpu);
		return OS_ERROR_NULL_POINTER;
	}

	if(msgType != SIP_MSG_REQUEST)
	{
		logError("expects msgType = SIP_MSG_REQUEST, but received msgType=%d.", msgType);
		return OS_ERROR_INVALID_VALUE;
	}

	//need to copy the TUMsg and DecodedRaw here when the message is directly from icscf via function call
	scscfReg_processRegMsg(pImpi, pImpu, osmemref(pSipTUMsg), osmemref(pReqDecodedRaw), true);

	return OS_STATUS_OK;
}


//msg directly from TR/TU layer
static void scscfReg_onSipRequest(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    if(!pSipTUMsg)
    {
        logError("null pointer, pSipTUMsg.");
        return;
    }

    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
	if(!pReqDecodedRaw)
	{
        logError("fails to sipDecodeMsgRawHdr.");
       	rspCode = SIP_RESPONSE_400;
       	goto EXIT;
	}

	osPointerLen_t impu;
    if(sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_TO]->pRawHdr->value, &impu) != OS_STATUS_OK)
    {
        logError("fails to sipParamUri_getUriFromRawHdrValue.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

	osPointerLen_t impi;
	if(cscf_getImpiFromSipMsg(pReqDecodedRaw, &impu, &impi) != OS_STATUS_OK)
	{
		logError("fails to cscf_getImpiFromSipMsg.")
		rspCode = SIP_RESPONSE_400;
		goto EXIT;
	}

	scscfReg_processRegMsg(&impi, &impu, pSipTUMsg, pReqDecodedRaw, false);

EXIT:
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		cscf_sendRegResponse(pSipTUMsg, pReqDecodedRaw, NULL, 0, pSipTUMsg->pPeer, pSipTUMsg->pLocal, rspCode);
	}

	return;
}


//common function for messages both from transaction or from icscf
static void scscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isViaIcscf)
{
    osStatus_e status = OS_STATUS_OK;

    sipHdrDecoded_t viaHdr={};
	
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    osPointerLen_t* pContactExpire = NULL;
    scscfRegInfo_t* pRegInfo = NULL;

    //check the expire header
    uint32_t regExpire = 0;
	sipTuRegTimeConfig_t regTimeConfig = {SCSCF_REG_MIN_EXPIRE, SCSCF_REG_MAX_EXPIRE, SCSCF_REG_DEFAULT_EXPIRE};
	sipTu_getRegExpireFromMsg(pReqDecodedRaw, &regExpire, regTimeConfig, &rspCode);
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		goto EXIT;
	}

	pRegInfo = osPlHash_getUserData(gScscfRegHash, pImpi, true);
    if(!pRegInfo)
    {
		if(regExpire == 0)
		{
			logInfo("received a out of blue deregister message, impi=%r", pImpi);
			rspCode = SIP_RESPONSE_200;
			goto EXIT;
		}

		pRegInfo = oszalloc(sizeof(scscfRegInfo_t), scscfRegInfo_cleanup);
		pRegInfo->tempWorkInfo.pRegHashLE = osPlHash_addUserData(gScscfRegHash, pImpi, true, pRegInfo);
		if(!pRegInfo->tempWorkInfo.pRegHashLE)
		{
			logError("fails to osPlHash_addUserData for impi(%r).", pImpi);
            rspCode = SIP_RESPONSE_500;
            goto EXIT;
        }
		
		mlogInfo(LM_CSCF, "impi(%r) is add into gScscfRegHash, pRegHashLE=%p", pImpi, pRegInfo->tempWorkInfo.pRegHashLE);

		pRegInfo->regState = SCSCF_REG_STATE_NOT_REGISTERED;			
		pRegInfo->tempWorkInfo.sarRegType = SCSCF_REG_SAR_REGISTER;	
	}
	else
	{
		if(regExpire == 0)
		{
			pRegInfo->tempWorkInfo.sarRegType = SCSCF_REG_SAR_DE_REGISTER;
		}
		else
		{
            pRegInfo->tempWorkInfo.sarRegType = SCSCF_REG_SAR_RE_REGISTER;
        }
	}

    //for icscf and scscf co-existence, each keeps its own copy of pSipTUMsg and pReqDecodedRaw, and free on its own independently
    pRegInfo->tempWorkInfo.impi = *pImpi;
    pRegInfo->tempWorkInfo.impu = *pImpu;
    pRegInfo->tempWorkInfo.pTUMsg = pSipTUMsg;
    pRegInfo->tempWorkInfo.pReqDecodedRaw = pReqDecodedRaw;
    pRegInfo->tempWorkInfo.sipLocalHost = isViaIcscf ? cscfConfig_getLocalSockAddr(CSCF_TYPE_ICSCF, false) : cscfConfig_getLocalSockAddr(CSCF_TYPE_SCSCF, false);

	if(SCSCF_IS_AUTH_ENABLED && scscfReg_isPerformMar(pRegInfo->regState, SCSCF_IS_REREG_PERFORM_AUTH))
	{
		if(scscfReg_performMar(&pRegInfo->tempWorkInfo.impi, &pRegInfo->tempWorkInfo.impu, pRegInfo) != OS_STATUS_OK)
    	{
       		logInfo("fails to scscfReg_performSar for impu(%r).", pImpu);
        	rspCode = SIP_RESPONSE_500;
    	}
		goto EXIT;
	}

	if(scscfReg_isPeformSar(pRegInfo->regState, regExpire))
	{
		if(scscfReg_performSar(&pRegInfo->tempWorkInfo.impi, &pRegInfo->tempWorkInfo.impu, pRegInfo, pRegInfo->tempWorkInfo.sarRegType, regExpire) != OS_STATUS_OK)
		{
			logInfo("fails to scscfReg_performSar for impu(%r).", pImpu);
			rspCode = SIP_RESPONSE_500;
			goto EXIT;
		}
	}

	//send back 200 OK
	cscf_sendRegResponse(pSipTUMsg, pReqDecodedRaw, pRegInfo, regExpire, pSipTUMsg->pPeer, pSipTUMsg->pLocal, SIP_RESPONSE_200);
	if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_RE_REGISTER)
	{
		pRegInfo->expiryTimerId = osRestartTimer(pRegInfo->expiryTimerId);
	}
	else if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_REGISTER)
	{
        pRegInfo->regState = SCSCF_REG_STATE_REGISTERED;
	}
		
	//start to perform 3rd party registration, not necessarily use the one received in the sip REGISTER
    pImpu = scscfReg_getNoBarImpu(&pRegInfo->ueList, true); //true=tel uri is preferred
    if(!pImpu)
    {
        logError("no no-barring impu is available.");
        goto EXIT;
    }
	
	//replace the originals tored impu with the new one
	pRegInfo->tempWorkInfo.impu = *pImpu;

	pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
	scscfIfcEvent_t ifcEvent = {true, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(pRegInfo->tempWorkInfo.sarRegType)}; 
	//perform 3rd party registration.  if SAR is performed, this function will be called when SAA returns
	bool is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent);
	if(is3rdPartyRegDone)
	{
   		pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
		if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_RE_REGISTER || pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_REGISTER)
		{
    		scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
		}
		else if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_DE_REGISTER)
		{
            scscfReg_deleteSubHash(pRegInfo);
		}
	}

EXIT:
    if(rspCode != SIP_RESPONSE_INVALID)
    {
        cscf_sendRegResponse(pSipTUMsg, pReqDecodedRaw, pRegInfo, 0, pSipTUMsg->pPeer, pSipTUMsg->pLocal, rspCode);
		if(pRegInfo->regState == SCSCF_REG_STATE_NOT_REGISTERED)
		{
            scscfReg_deleteSubHash(pRegInfo);
		}
    }

	return;
}



//this function will recursively calling itself if fails to send REGISTER to 3rd party AS until no more AS to try (for sessCase=continued)
bool scscfReg_perform3rdPartyReg(scscfRegInfo_t* pRegInfo, scscfIfcEvent_t* pIfcEvent)
{
	bool isDone = false;
    dnsResResponse_t* pDnsResponse = NULL;

	if(!pRegInfo || osList_isEmpty(&pRegInfo->ueList))
	{
		logError("pRegInfo(%p) is null or ueList is empty.", pRegInfo);
		isDone = true;
		goto EXIT;
	}

    pRegInfo->tempWorkInfo.pAs = scscfIfc_getNextAS(&pRegInfo->tempWorkInfo.pLastIfc, &pRegInfo->userProfile.sIfcIdList, pRegInfo->tempWorkInfo.pReqDecodedRaw, pIfcEvent);
    if(!pRegInfo->tempWorkInfo.pAs)
    {
        isDone = true;
        goto EXIT;
    }

    sipTuUri_t targetUri = {*pRegInfo->tempWorkInfo.pAs, true};
    sipTuAddr_t nextHop = {};
    sipTu_convertUri2NextHop(&targetUri, &nextHop.ipPort);

	//if nextHop is FQDN, perform DNS query
    if(!osIsIpv4(&nextHop.ipPort.ip))
    {
		dnsQueryStatus_e dnsQueryStatus = dnsQuery(&nextHop.ipPort.ip, nextHop.ipPort.port ? DNS_QTYPE_A : DNS_QTYPE_NAPTR, true, true, &pDnsResponse, scscfReg_dnsCallback, pRegInfo);
		if(dnsQueryStatus == DNS_QUERY_STATUS_FAIL)
		{
			logError("fails to perform dns query for %r.", &nextHop.ipPort.ip);
			pIfcEvent->isLastAsOK = false;
        	isDone = scscfReg_perform3rdPartyReg(pRegInfo, pIfcEvent);
        	goto EXIT;
		}

		//waiting for dns query response
		if(!pDnsResponse)
		{
			goto EXIT;
		}
		
		//for now, assume tcp and udp always use the same port.  improvement can be done to allow different port, in this case, sipProxy_forwardReq() pNextHop needs to pass in both tcp and udp nextHop info for it to choose, like based on message size, etc. to-do
		if(!sipTu_getBestNextHop(pDnsResponse, true, &nextHop))
		{
			logError("could not find the next hop for %r.", &nextHop.ipPort.ip);

	    	pIfcEvent->isLastAsOK = false;
       		isDone = scscfReg_perform3rdPartyReg(pRegInfo, pIfcEvent);
        	goto EXIT;
    	}
	}
	
	if(scscfReg_forwardSipRegister(pRegInfo, &nextHop) != OS_STATUS_OK)
	{
		pIfcEvent->isLastAsOK = false;
        isDone = scscfReg_perform3rdPartyReg(pRegInfo, pIfcEvent);
        goto EXIT;
	}

EXIT:
    osfree(pDnsResponse);
	return isDone;
}


static void scscfReg_dnsCallback(dnsResResponse_t* pRR, void* pData)
{
	bool is3rdPartyRegDone = false;
	if(!pRR || !pData)
	{
		logError("null pointer, pRR=%p, pData=%p.", pRR, pData);
		return;
	}

	scscfRegInfo_t* pRegInfo = pData;	
    sipTuAddr_t nextHop = {};
	scscfIfcEvent_t ifcEvent = {false, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(pRegInfo->tempWorkInfo.sarRegType)};
	if(!sipTu_getBestNextHop(pRR, true, &nextHop))
	{
		logError("could not find the next hop for %r.", &nextHop.ipPort.ip);

		is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent);
		goto EXIT;
	}

    if(scscfReg_forwardSipRegister(pRegInfo, &nextHop) != OS_STATUS_OK)
    {
        is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent);
        goto EXIT;
    }

EXIT:
    osfree(pRR);

	if(is3rdPartyRegDone)
	{
        pRegInfo->tempWorkInfo.regWorkState =  SCSCF_REG_WORK_STATE_NONE;
        if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_RE_REGISTER || pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_REGISTER)
        {
            scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
        }
        else if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_DE_REGISTER)
        {
			scscfReg_deleteSubHash(pRegInfo);
        }
    }
}


//receiving sip response from an AS for 3rd party registration
static osStatus_e scscfReg_onSipResponse(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipTUMsg || !pSipTUMsg->pTUId)
    {
        logError("null pointer, pSipTUMsg=%p, pSipTUMsg->pTUId=%p.", pSipTUMsg, pSipTUMsg->pTUId);
        status = OS_ERROR_NULL_POINTER;
		goto EXIT;
    }

    logInfo("received a sip response, rspCode=%d.", pSipTUMsg->sipMsgBuf.rspCode);

    scscfRegInfo_t* pRegInfo = ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo;
    if(!pRegInfo)
    {
        logError("null pointer, pRegInfo is null.");
        status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
    }

	switch(pRegInfo->tempWorkInfo.regWorkState)
	{
		case SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE:
		{
    		//continue to perform 3rd party registration.
    		bool isLastAsOk = sipMsg_isRsp2xx(pSipTUMsg->sipMsgBuf.rspCode) ? true : false;
    		scscfIfcEvent_t ifcEvent = {isLastAsOk, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(pRegInfo->tempWorkInfo.sarRegType)};
    		bool is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent);
    		if(is3rdPartyRegDone)
    		{
        		pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_NONE;
        		scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);

        		//for dereg, free the ue's subscription
        		if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_DE_REGISTER)
        		{
            		scscfReg_deleteSubHash(pRegInfo);
        		}
			}

			break;
		}
		case SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_NW_DEREG_RESPONSE:
		{
			bool isNWDeregDone = scscfReg_performNWDeRegister(pRegInfo);
			if(isNWDeregDone)
			{
				scscfReg_deleteSubHash(pRegInfo);
			}

			break;
		}
		default:
        	logError("pRegInfo->tempWorkInfo.regWorkState(%d) is not SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE.", pRegInfo->tempWorkInfo.regWorkState);
        	status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
    }

EXIT:
	osfree(pSipTUMsg);
    return status;
}


//make sure dereg purge timer is long enough so that for any possible resp TrFailure error (include TrFailure error for dereg request retransmission), it ALWAYS happens within the purge timer period. (may not have to be, see comment in  pRegInfo = pSipTUMsg->pTUId assignment.
static osStatus_e scscfReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipTUMsg)
    {
        logError("null pointer, pSipTUMsg");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pSipTUMsg->errorInfo.isServerTransaction)
	{
		logInfo("fails to send response for the received SIP REGISTER. just ignore, expect UE to re-register.");
		goto EXIT;
	}

    scscfRegInfo_t* pRegInfo = ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo;
    if(!pRegInfo)
    {
        logError("pRegInfo is NULL.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//quarantine the destination
    if((pSipTUMsg->sipTuMsgType == SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE))
    {
        sipTu_setDestFailure(pRegInfo->tempWorkInfo.pAs, pSipTUMsg->pPeer);
	}

    switch(pRegInfo->tempWorkInfo.regWorkState)
    {
        case SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE:
        {
			bool is3rdPartyRegDone = false;
			scscfIfcEvent_t ifcEvent = {false, SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(pRegInfo->tempWorkInfo.sarRegType)};
			if(is3rdPartyRegDone == scscfReg_perform3rdPartyReg(pRegInfo, &ifcEvent))
			{
				pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_NONE;
				scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);

				//for dereg, free the ue's subscription
				if(pRegInfo->tempWorkInfo.sarRegType == SCSCF_REG_SAR_DE_REGISTER)
				{
					scscfReg_deleteSubHash(pRegInfo);
        		}
			}

			break;
    	}
        case SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_NW_DEREG_RESPONSE:
        {
            bool isNWDeregDone = scscfReg_performNWDeRegister(pRegInfo);
            if(isNWDeregDone)
            {
                scscfReg_deleteSubHash(pRegInfo);
            }

            break;
        }
        default:
            logError("pRegInfo->tempWorkInfo.regWorkState(%d) is not SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE.", pRegInfo->tempWorkInfo.regWorkState);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
    }

EXIT:
	osfree(pSipTUMsg);
    return status;
}


static osStatus_e scscfReg_forwardSipRegister(scscfRegInfo_t* pRegInfo, sipTuAddr_t* pNextHop)
{
	osStatus_e status = OS_STATUS_OK;

    //build a sip register message towards an AS and forward.  May also to use sipTU_uacBuildRequest() and sipMsgAppendHdrStr(), instead, sipProxy_forwardReq() is chosen.
	int len = 0;
    sipProxy_msgModInfo_t msgModInfo = {false, 0};
	msgModInfo.isChangeCallId = true;

	//add to header
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_TO, &pRegInfo->tempWorkInfo.impu);

	//add from header
	sipPointerLen_t fromHdr = SIPPL_INIT(fromHdr);
	sipPointerLen_t tagId = SIPPL_INIT(tagId);
	sipHdrFromto_generateSipPLTagId(&tagId, true);
	len = osPrintf_buffer((char*)fromHdr.pl.p, SIP_HDR_MAX_SIZE, "%s;%r", SCSCF_URI_WITH_PORT, &tagId.pl);
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_FROM, &fromHdr.pl);

	//add route
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_ROUTE, pRegInfo->tempWorkInfo.pAs);

	//add contact
	osPointerLen_t scscfContact = {SCSCF_URI_WITH_PORT, strlen(SCSCF_URI_WITH_PORT)};
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_CONTACT, &scscfContact);

	//add P-Charging-Function-Addresses
	sipPointerLen_t chgInfo = SIPPL_INIT(chgInfo);
	if(pRegInfo->hssChgInfo.chgAddrType != CHG_ADDR_TYPE_INVALID)
	{
		len = osPrintf_buffer((char*)chgInfo.pl.p, SIP_HDR_MAX_SIZE, "%s=\"%r\"", pRegInfo->hssChgInfo.chgAddrType == CHG_ADDR_TYPE_CCF ? "ccf" : "ecf", &pRegInfo->hssChgInfo.chgAddr.pl);
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

    proxyInfo_t* pProxyInfo = oszalloc(sizeof(proxyInfo_t), NULL);
    pProxyInfo->proxyOnMsg = NULL;		//for cscf proxyOnMsg is not used since cscf does not distribute message further after receiving from TU
    pProxyInfo->pCallInfo = pRegInfo;
    
	sipTuUri_t targetUri = {*pRegInfo->tempWorkInfo.pAs, true};
	void* pTransId = NULL;
	status = sipProxy_forwardReq(pRegInfo->tempWorkInfo.pTUMsg, pRegInfo->tempWorkInfo.pReqDecodedRaw, &targetUri, &msgModInfo, pNextHop, false, pProxyInfo, &pTransId);
    if(status != OS_STATUS_OK || !pTransId)
    {
        logError("fails to forward sip request, status=%d, pTransId=%p.", status, pTransId);
		status = pTransId ? status : OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
    }

    scscfAsRegInfo_t* pAsInfo = osmalloc(sizeof(scscfAsRegInfo_t), NULL);
    pAsInfo->asAddr = *pNextHop;
    pAsInfo->asUri = *pRegInfo->tempWorkInfo.pAs;
    pAsInfo->callId = msgModInfo.newCallId;
    osList_append(&pRegInfo->asRegInfoList, pAsInfo);

EXIT:
	return status;
}	


static bool scscfReg_performNWDeRegister(scscfRegInfo_t* pRegInfo)
{
	bool isDone = false;

    if(!pRegInfo)
    {
        logError("null pointer, pRegInfo=%p.", pRegInfo);
		isDone = true;
        goto EXIT;
    }

	switch(pRegInfo->tempWorkInfo.regWorkState)
	{
		case SCSCF_REG_WORK_STATE_NONE:
			pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_NW_DEREG_RESPONSE;
    		pRegInfo->tempWorkInfo.impu = *scscfReg_getNoBarImpu(&pRegInfo->ueList, true); //true=tel uri is preferred

		case SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_NW_DEREG_RESPONSE:
		{
			osListElement_t* pLE = osList_popHead(&pRegInfo->asRegInfoList);
			if(pLE)
			{
				scscfReg_createAndSendSipDeRegister(pRegInfo, pLE->data);
				osfree(pLE->data);
				osfree(pLE);
			}
			else
			{
				isDone = true;
			}
			break;
		}
		default:
			logError("regWorkState(%d) is not in state allowed to perform NW deregister.", pRegInfo->tempWorkInfo.regWorkState);
			isDone = true;
			break;
	}
	
EXIT:
	return isDone;
}
	

static osStatus_e scscfReg_createAndSendSipDeRegister(scscfRegInfo_t* pRegInfo, scscfAsRegInfo_t* pAsInfo)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pRegInfo || !pAsInfo)
	{
		logError("null pointer, pRegInfo=%p, pAsInfo=%p.", pRegInfo, pAsInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    sipTransInfo_t sipTransInfo;
    osIpPort_t ipPort = {{{SCSCF_IP_ADDR, strlen(SCSCF_IP_ADDR)}}, 0};
    sipTransInfo.transId.viaId.host = ipPort.ip.pl;
    sipTransInfo.transId.viaId.port = ipPort.port;

    size_t topViaProtocolPos = 0;
	sipTuUri_t reqUri = {pAsInfo->asUri, true};
	osPointerLen_t caller = {SCSCF_URI_WITH_PORT, strlen(SCSCF_URI_WITH_PORT)};
//create a UAC request with req line, via, from, to, callId and max forward.  Other headers needs to be added by user as needed
//be noted this function does not include the extra "\r\n" at the last of header, user needs to add it when completing the creation of a SIP message
	osMBuf_t* pReq = sipTU_uacBuildRequest(SIP_METHOD_REGISTER, &reqUri, &pRegInfo->tempWorkInfo.impu, &caller, &sipTransInfo.transId.viaId, &pAsInfo->callId.pl, &topViaProtocolPos);
    if(!pReq)
    {
        logError("fails to sipTU_uacBuildRequest() for impu(%r) towards AS(%r).", &pRegInfo->tempWorkInfo.impu, &pAsInfo->asUri);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    //now add other headers. add cSeq, p-asserted-id, content-type
    osPointerLen_t cSeqHdr = {"1 REGISTER", sizeof("1 REGISTER")-1};
    status = sipMsgAppendHdrStr(pReq, "Cseq", &cSeqHdr, 0);

    status = sipMsgAppendHdrStr(pReq, "Content-Length", NULL, 0);
	sipTU_msgBuildEnd(pReq, false);

    mlogInfo(LM_CSCF, "SIP Request Message=\n%M", pReq);

    sipTransMsg_t sipTransMsg;
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
    sipTransMsg.isTpDirect = false;
    sipTransMsg.appType = SIPTU_APP_TYPE_SCSCF_REG;

    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = SIP_METHOD_REGISTER;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;

    sipTransInfo.isRequest = true;
    sipTransInfo.transId.reqCode = SIP_METHOD_REGISTER;
    sipTransMsg.request.pTransInfo = &sipTransInfo;
	sipTransMsg.request.sipTrMsgBuf.tpInfo.peer = pAsInfo->asAddr.sockAddr;
	sipTransMsg.request.sipTrMsgBuf.tpInfo.local = cscfConfig_getLocalSockAddr(CSCF_TYPE_SCSCF, true);
    sipTransMsg.request.sipTrMsgBuf.tpInfo.protocolUpdatePos = topViaProtocolPos;
    sipTransMsg.pTransId = NULL;
    sipTransMsg.pSenderId = pRegInfo;

    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

    osfree(pReq);

EXIT:
	return status;
}


static uint64_t scscfReg_startTimer(time_t msec, scscfRegInfo_t* pRegInfo)
{
    return osStartTimer(msec, scscfReg_onTimeout, pRegInfo);
}

		
void scscfReg_onTimeout(uint64_t timerId, void* data)
{
	logInfo("timeout, timerId=0x%lx.", timerId); 
	if(!data)
	{
		logError("null pointer, data.");
		return;
	}

	scscfRegInfo_t* pRegInfo = data;

	if(timerId == pRegInfo->expiryTimerId)
	{
		pRegInfo->expiryTimerId = 0;
        bool isNWDeregDone = scscfReg_performNWDeRegister(pRegInfo);
        if(isNWDeregDone)
        {
            scscfReg_deleteSubHash(pRegInfo);
        }
	}
	else
	{
		logError("received a unrecognized tiemrId.");
	}

EXIT:
	return;
}


static uint64_t scscfReg_restartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, scscfReg_onTimeout, pData);
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
	if(OS_STATUS_OK != scscfReg_decodeHssMsg(pDiaDecoded, pRegInfo, &resultCode))
	{
		logError("fails to scscfReg_decodeHssMsg for pRegInfo(%p).", pRegInfo);
		//to-do, needs to free hash, and notify sessions
		goto EXIT;
	}

	switch(pDiaDecoded->cmdCode)
	{
		case DIA_CMD_CODE_SAR:
			scscfReg_onSaa(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_MAR:
			scscfReg_onMaa(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_PPR:
            //scscfReg_onPpr(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_RTR:
            //scscfReg_onRtr(pRegInfo, resultCode);
			break;
		default:
			logError("scscf receives dia cmdCode(%d) that is not supported, ignore.", pDiaDecoded->cmdCode);
			break;
	}
			
EXIT:
	return;	
}


static osPointerLen_t* scscfReg_getNoBarImpu(osList_t* pUeList, bool isTelPreferred)
{
	osPointerLen_t* pImpu = NULL;

	if(!pUeList)
	{
		goto EXIT;
	}

	osListElement_t* pLE = pUeList->head;
	while(pLE)
	{
		scscfRegIdentity_t* pId = pLE->data;
		if(!pId->isImpi && !pId->impuInfo.isBarred)
		{
			
			if(isTelPreferred)
			{
				if(pId->impuInfo.impu.p[0] == 't' || pId->impuInfo.impu.p[0] == 'T')
				{
					pImpu = &pId->impuInfo.impu;
					goto EXIT;
				}
				else if(!pImpu)
				{
					pImpu = &pId->impuInfo.impu;
				}
			}
			else
			{
				pImpu = &pId->impuInfo.impu;
				goto EXIT;
			}
		}

		pLE = pLE->next;
	}

EXIT:
	return pImpu;
}
				

void scscfReg_createSubHash(scscfRegInfo_t* pRegInfo)
{
    //go through all ID list, and add one at a time.
    osListElement_t* pLE = pRegInfo->ueList.head;
    while(pLE)
    {
        scscfRegIdentity_t* pRegIdentity = pLE->data;
        osPointerLen_t* pId = pRegIdentity->isImpi ? &pRegIdentity->impi : &pRegIdentity->impuInfo.impu;
        osListElement_t* pHashLE = osPlHash_getElement(gScscfRegHash, pId, true);
        if(pHashLE)
        {
            scscfRegInfo_t* pOldRegInfo = osHash_replaceUserData(gScscfRegHash, pHashLE, osmemref(pRegInfo));
            osfree(pOldRegInfo);
        }
        else
        {
            pHashLE = osPlHash_addUserData(gScscfRegHash, pId, true, osmemref(pRegInfo));
        }
        pRegIdentity->pRegHashLE = pHashLE;

        pLE = pLE->next;
    }
}

	
//delete hash data for the whole subscription of a UE
void scscfReg_deleteSubHash(scscfRegInfo_t* pRegInfo)
{
	//go through all ID list, and delete them.
	osListElement_t* pLE = pRegInfo->ueList.head;
	if(!pLE)
	{
		osHash_deleteNode(pRegInfo->tempWorkInfo.pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
	}
	else
	{
		while(pLE)
		{	
    		osHash_deleteNode(((scscfRegIdentity_t*)pLE->data)->pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
			pLE = pLE->next;
		}
	}
}


static void scscfRegInfo_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	scscfRegInfo_t* pRegInfo = data;

	pRegInfo->regState = SCSCF_REG_STATE_NOT_REGISTERED;
	osList_delete(&pRegInfo->ueList);
	osList_delete(&pRegInfo->asRegInfoList);

	if(pRegInfo->expiryTimerId)
	{
		pRegInfo->expiryTimerId = osStopTimer(pRegInfo->expiryTimerId);
	}

	if(pRegInfo->purgeTimerId)
	{
		pRegInfo->purgeTimerId = osStopTimer(pRegInfo->purgeTimerId);
	}

	scscfRegTempWorkInfo_cleanup(&pRegInfo->tempWorkInfo);
}


void scscfRegTempWorkInfo_cleanup(scscfRegTempWorkInfo_t* pTempWorkInfo)
{
	pTempWorkInfo->regWorkState = SCSCF_REG_WORK_STATE_NONE;
	pTempWorkInfo->sarRegType = SCSCF_REG_SAR_INVALID;
    osfree(pTempWorkInfo->pTUMsg);
    osfree(pTempWorkInfo->pReqDecodedRaw);
    pTempWorkInfo->pRegHashLE = NULL;
    pTempWorkInfo->pLastIfc = NULL;
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


static inline bool scscfReg_isPerformMar(scscfRegState_e regState, bool isAuthForReReg)
{
	return (regState != SCSCF_REG_STATE_REGISTERED ||(regState == SCSCF_REG_STATE_REGISTERED && isAuthForReReg));
}


static inline bool scscfReg_isPeformSar(scscfRegState_e regState, int regExpire)
{
	return (regState != SCSCF_REG_STATE_REGISTERED || (regState == SCSCF_REG_STATE_REGISTERED && !regExpire));
}
