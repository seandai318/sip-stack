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
#include "sipUri.h"

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

static bool scscfReg_performNWDeRegister(scscfRegInfo_t* pRegInfo);
static osStatus_e scscfReg_createAndSendSipDeRegister(scscfRegInfo_t* pRegInfo, scscfAsRegInfo_t* pAsInfo);

static void scscfRegInfo_cleanup(void* data);

static inline osPointerLen_t* scscfReg_getUserId(scscfRegIdentity_t* pIdentity);
static inline bool scscfReg_isPerformMar(scscfRegState_e regState, bool isAuthForReReg);
static inline bool scscfReg_isPeformSar(scscfRegState_e regState, int regExpire);
static osStatus_e scscfIfc_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode);

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
	mlogInfo(LM_CSCF, "msgType=%d.", msgType);

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
    if(sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_TO]->pRawHdr->value, false, &impu) != OS_STATUS_OK)
    {
        logError("fails to sipParamUri_getUriFromRawHdrValue.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
    }

	osPointerLen_t impi;
	if(cscf_getImpiFromSipMsg(pReqDecodedRaw, &impu, &impi) != OS_STATUS_OK)
	{
		logError("fails to cscf_getImpiFromSipMsg.");
		rspCode = SIP_RESPONSE_400;
		goto EXIT;
	}

    mlogInfo(LM_CSCF, "process a SIP request, impi=%r, impu=%r.", &impi, &impu);

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

    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    osPointerLen_t* pContactExpire = NULL;
    scscfRegInfo_t* pRegInfo = NULL;

    //check the expire header
    uint32_t regExpire = 0;
	sipTuRegTimeConfig_t regTimeConfig = {SCSCF_REG_MIN_EXPIRE, SCSCF_REG_MAX_EXPIRE, SCSCF_REG_DEFAULT_EXPIRE};
	sipTu_getRegExpireFromMsg(pReqDecodedRaw, &regExpire, &regTimeConfig, &rspCode);
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		goto EXIT;
	}

	mlogInfo(LM_CSCF, "regExpire=%d.", regExpire);

	pRegInfo = osPlHash_getUserData(gScscfRegHash, pImpi, true);
	mdebug(LM_CSCF, "pImpi=%r, pRegInfo=%p.", pImpi, pRegInfo);
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

	//update ueContactInfo
	pRegInfo->ueContactInfo.regExpire = regExpire;
	if(regExpire)
	{
		//if there is contact change
		if(osPL_strplcmp(pRegInfo->ueContactInfo.regContact.rawHdr.buf, pRegInfo->ueContactInfo.regContact.rawHdr.size, &pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr->value, true) != 0)
		{
			sipHdrDecoded_cleanup(&pRegInfo->ueContactInfo.regContact);

			status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, &pRegInfo->ueContactInfo.regContact, true);
			if(status != OS_STATUS_OK)
			{
				logError("fails to get contact URI for impi(%r).", pImpi);
				rspCode = SIP_RESPONSE_403;
				goto EXIT;
			}
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
		}

		goto EXIT;
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
    pImpu = scscfReg_getNoBarImpu(pRegInfo->ueList, pRegInfo->regInfoUENum, true); //true=tel uri is preferred
    if(!pImpu)
    {
        logError("no no-barring impu is available.");
        goto EXIT;
    }
	
	//replace the originals tored impu with the new one
	pRegInfo->tempWorkInfo.impu = *pImpu;

	pRegInfo->tempWorkInfo.regWorkState = SCSCF_REG_WORK_STATE_WAIT_3RD_PARTY_REG_RESPONSE;
	//perform 3rd party registration.  if SAR is performed, this function will be called when SAA returns
	bool is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
	if(is3rdPartyRegDone)
	{
        logInfo("3rd party registration is completed for impi=%r.", &pRegInfo->tempWorkInfo.impi);
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
		if(pRegInfo && pRegInfo->regState == SCSCF_REG_STATE_NOT_REGISTERED)
		{
            scscfReg_deleteSubHash(pRegInfo);
		}
    }

	return;
}



//this function will recursively calling itself if fails to send REGISTER to 3rd party AS until no more AS to try (for sessCase=continued)
bool scscfReg_perform3rdPartyReg(scscfRegInfo_t* pRegInfo)
{
	bool isDone = false;
    dnsResResponse_t* pDnsResponse = NULL;

	if(!pRegInfo || pRegInfo->regInfoUENum == 0)
	{
		logError("pRegInfo(%p) is null or ueList is empty.", pRegInfo);
		isDone = true;
		goto EXIT;
	}

    scscfIfcEvent_t ifcEvent = {SIP_METHOD_REGISTER, SCSCF_IFC_SESS_CASE_ORIGINATING, scscfIfc_mapSar2IfcRegType(pRegInfo->tempWorkInfo.sarRegType)};
    pRegInfo->tempWorkInfo.pAs = scscfIfc_getNextAS(&pRegInfo->tempWorkInfo.pLastIfc, &pRegInfo->userProfile.sIfcIdList, pRegInfo->tempWorkInfo.pReqDecodedRaw, &ifcEvent, &pRegInfo->tempWorkInfo.isIfcContinuedDH);
    if(!pRegInfo->tempWorkInfo.pAs)
    {
		mdebug(LM_CSCF, "no more AS, 3rd party registration for IMPI(%r), IMPU(%r) is completed.", &pRegInfo->tempWorkInfo.impi, &pRegInfo->tempWorkInfo.impu);
        isDone = true;
        goto EXIT;
    }

    mdebug(LM_CSCF, "next hop is %r", pRegInfo->tempWorkInfo.pAs);

    sipTuUri_t targetUri = {{*pRegInfo->tempWorkInfo.pAs}, true};
	sipTuNextHop_t  nextHop = {};
    sipTu_convertUri2NextHop(&targetUri, &nextHop);

	//if nextHop is FQDN, perform DNS query
    if(!osIsIpv4(&nextHop.nextHop.ipPort.ip))
    {
		dnsQueryStatus_e dnsQueryStatus = dnsQuery(&nextHop.nextHop.ipPort.ip, nextHop.nextHop.ipPort.port ? DNS_QTYPE_A : DNS_QTYPE_NAPTR, true, true, &pDnsResponse, scscfReg_dnsCallback, pRegInfo);
		if(dnsQueryStatus == DNS_QUERY_STATUS_FAIL)
		{
			logError("fails to perform dns query for %r.", &nextHop.nextHop.ipPort.ip);
			if(pRegInfo->tempWorkInfo.isIfcContinuedDH)
			{
        		isDone = scscfReg_perform3rdPartyReg(pRegInfo);
			}
			else
			{
				isDone = true;
			}
        	goto EXIT;
		}

		//waiting for dns query response
		if(!pDnsResponse)
		{
			goto EXIT;
		}
		
		//for now, assume tcp and udp always use the same port.  improvement can be done to allow different port, in this case, sipProxy_forwardReq() pNextHop needs to pass in both tcp and udp nextHop info for it to choose, like based on message size, etc. to-do
		if(!sipTu_getBestNextHop(pDnsResponse, true, &nextHop.nextHop, NULL))
		{
			logError("could not find the next hop for %r.", &nextHop.nextHop.ipPort.ip);
            if(pRegInfo->tempWorkInfo.isIfcContinuedDH)
	    	{	
       			isDone = scscfReg_perform3rdPartyReg(pRegInfo);
			}
			else
			{
				isDone = true;
			}
        	goto EXIT;
    	}
	}

	if(scscfReg_forwardSipRegister(pRegInfo, &nextHop.nextHop) != OS_STATUS_OK)
	{
		if(pRegInfo->tempWorkInfo.isIfcContinuedDH)
		{
        	isDone = scscfReg_perform3rdPartyReg(pRegInfo);
		}
		else
		{
			isDone = true;
		}
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

	if(pRR->rrType == DNS_RR_DATA_TYPE_STATUS)
	{
		logInfo("dns query failed, qName=%r, resStatus=%d, dnsRCode=%d", pRR->status.pQName, pRR->status.resStatus, pRR->status.dnsRCode);

        is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
		goto EXIT;
    }

    sipTuAddr_t nextHop = {};
	if(!sipTu_getBestNextHop(pRR, false, &nextHop, NULL))
	{
		logError("could not find the next hop for %r.", &nextHop.ipPort.ip);

		is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
		goto EXIT;
	}

	mdebug(LM_CSCF, "send 3rd party register to %A.", &nextHop.sockAddr);
    if(scscfReg_forwardSipRegister(pRegInfo, &nextHop) != OS_STATUS_OK)
    {
		if(pRegInfo->tempWorkInfo.isIfcContinuedDH)
		{
        	is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
		}
		else
		{
			is3rdPartyRegDone = true;
		}
        goto EXIT;
    }

EXIT:
    osfree(pRR);

	if(is3rdPartyRegDone)
	{
		logInfo("3rd party registration is completed for impi=%r.", &pRegInfo->tempWorkInfo.impi); 
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

    logInfo("received a sip response, rspCode=%d, pRegInfo=%p.", pSipTUMsg->sipMsgBuf.rspCode, pSipTUMsg->pTUId);

    scscfRegInfo_t* pRegInfo = pSipTUMsg->pTUId;
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
			bool is3rdPartyRegDone = true;
			if(isLastAsOk || (!isLastAsOk && pRegInfo->tempWorkInfo.isIfcContinuedDH))
			{ 
    			is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
			}

    		if(is3rdPartyRegDone)
    		{
		        logInfo("3rd party registration is completed for impi=%r.", &pRegInfo->tempWorkInfo.impi);
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

	logInfo("receive a TrFailure, sipTuMsgType=%d, pRegInfo=%p", pSipTUMsg->sipTuMsgType, pSipTUMsg->pTUId);

	scscfRegInfo_t* pRegInfo = pSipTUMsg->pTUId;
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
            bool is3rdPartyRegDone = true;
            if(pRegInfo->tempWorkInfo.isIfcContinuedDH)
            {
                is3rdPartyRegDone = scscfReg_perform3rdPartyReg(pRegInfo);
			}
			if(is3rdPartyRegDone)
			{
		        logInfo("3rd party registration is completed for impi=%r.", &pRegInfo->tempWorkInfo.impi);
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
    sipProxy_msgModInfo_t msgModInfo = {false, 0};
	msgModInfo.isChangeCallId = true;

	//add to header
	sipTuHdrRawValue_t hdrToRawValue = {SIPTU_RAW_VALUE_TYPE_STR_PTR, {&pRegInfo->tempWorkInfo.impu}};
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_TO, &hdrToRawValue);

	//add from header
	sipPointerLen_t fromHdr = SIPPL_INIT(fromHdr);
	sipPointerLen_t tagId = SIPPL_INIT(tagId);
	sipHdrFromto_generateSipPLTagId(&tagId, true);
	sipTuHdrRawValue_t hdrFromRawValue = {SIPTU_RAW_VALUE_TYPE_STR_PTR, {&fromHdr.pl}};
	fromHdr.pl.l = osPrintf_buffer((char*)fromHdr.pl.p, SIP_HDR_MAX_SIZE, "%s;%r", SCSCF_URI_WITH_PORT, &tagId.pl);
	if(fromHdr.pl.l < 0)
	{
        logError("fails to osPrintf_buffer for From header.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;	
	}
	else
	{
		sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_FROM, &hdrFromRawValue);
	}

	//add route
	sipTuHdrRawValue_t hdrRouteRawValue = {SIPTU_RAW_VALUE_TYPE_STR_PTR, {pRegInfo->tempWorkInfo.pAs}};
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_ROUTE, &hdrRouteRawValue);

	//add contact
	osPointerLen_t scscfContact = {SCSCF_URI_WITH_PORT, strlen(SCSCF_URI_WITH_PORT)};
	sipTuHdrRawValue_t hdrContactRawValue = {SIPTU_RAW_VALUE_TYPE_STR_PTR, {&scscfContact}};
	sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_CONTACT, &hdrContactRawValue);

	//add expires if the register from UE does not have Expires header
	sipTuHdrRawValue_t hdrExpiresRawValue = {SIPTU_RAW_VALUE_TYPE_INT, {.intValue=pRegInfo->ueContactInfo.regExpire}};
	if(!pRegInfo->tempWorkInfo.pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES])
	{
		sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_EXPIRES, &hdrExpiresRawValue);
	}

	//add P-Charging-Function-Addresses
	sipPointerLen_t chgInfo = SIPPL_INIT(chgInfo);
	sipTuHdrRawValue_t hdrChgRawValue = {SIPTU_RAW_VALUE_TYPE_STR_PTR, {&chgInfo.pl}};
	if(pRegInfo->hssChgInfo.chgAddrType != CHG_ADDR_TYPE_INVALID)
	{
		chgInfo.pl.l = osPrintf_buffer((char*)chgInfo.pl.p, SIP_HDR_MAX_SIZE, "%s=\"%r\"", pRegInfo->hssChgInfo.chgAddrType == CHG_ADDR_TYPE_CCF ? "ccf" : "ecf", &pRegInfo->hssChgInfo.chgAddr.pl);
		if(chgInfo.pl.l <0)
		{
			logError("fails to osPrintf_buffer for P-Charging-Function-Addresses, ignore this header.");
        }
		else
		{
			sipProxyMsgModInfo_addHdr(msgModInfo.extraAddHdr, &msgModInfo.addNum, SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES, &hdrChgRawValue);		
		}
	}

	//delete hdrs
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_TO, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_FROM, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_ROUTE, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_REQUIRE, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_SUPPORTED, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_PATH, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_CONTACT, true);
	sipProxyMsgModInfo_delHdr(msgModInfo.extraDelHdr, &msgModInfo.delNum, SIP_HDR_AUTHORIZATION, true);

	sipTuUri_t targetUri = {*pRegInfo->tempWorkInfo.pAs, true};
	void* pTransId = NULL;
	status = sipProxy_forwardReq(SIPTU_APP_TYPE_SCSCF_REG, pRegInfo->tempWorkInfo.pTUMsg, pRegInfo->tempWorkInfo.pReqDecodedRaw, &targetUri, &msgModInfo, pNextHop, false, pRegInfo, &pTransId);
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
			
    		osPointerLen_t* pNoBarImpu = scscfReg_getNoBarImpu(pRegInfo->ueList, pRegInfo->regInfoUENum, true); //true=tel uri is preferred
			if(!pNoBarImpu)
    		{
        		logError("no no-barring impu is available, remove the registration locally.");
				isDone = true;
        		goto EXIT;
    		}
			pRegInfo->tempWorkInfo.impu = *pNoBarImpu;
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

	scscfRegInfo_t* pRegInfo = pAppData;
	diaResultCode_t resultCode;
	if(OS_STATUS_OK != scscfIfc_decodeHssMsg(pDiaDecoded, pRegInfo, &resultCode))
	{
		logError("fails to scscfIfc_decodeHssMsg for pRegInfo(%p).", pRegInfo);
		//to-do, needs to free hash, and send back reg failure
		goto EXIT;
	}

    mdebug(LM_CSCF, "dia cmdCode=%d, resultCode=%d.", pDiaDecoded->cmdCode, resultCode.resultCode);

	switch(pDiaDecoded->cmdCode)
	{
		case DIA_CMD_CODE_SAR:
			scscfReg_onSaa(pRegInfo, resultCode);
			break;
        case DIA_CMD_CODE_MAR:
			//scscfReg_onMaa(pRegInfo, resultCode);
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
	//no need to osfree pDiaDecoded since app did not ref it
	return;	
}


//scscfSess received SAA for unregistered user case
void* scscfReg_onSessSaa(diaMsgDecoded_t* pDiaDecoded, sipResponse_e* rspCode, sIfcIdList_t* pSIfcIdList)
{
	osStatus_e status = OS_STATUS_OK;
	*rspCode = SIP_RESPONSE_200;
	scscfRegInfo_t* pRegInfo = NULL;

	if(pDiaDecoded->cmdCode != DIA_CMD_CODE_SAR && (pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST))
	{
		logError("received diameter message is not SAA(cmdCode=%d).", pDiaDecoded->cmdCode);
		*rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pRegInfo = oszalloc(sizeof(scscfRegInfo_t), scscfRegInfo_cleanup);

    diaResultCode_t resultCode;
	status = scscfIfc_decodeHssMsg(pDiaDecoded, pRegInfo, &resultCode);
	if(status != OS_STATUS_OK)
    {
        logError("fails to scscfIfc_decodeHssMsg for SAA for pRegInfo(%p).", pRegInfo);
		*rspCode = SIP_RESPONSE_500;
        goto EXIT;
    }

	*rspCode = cscf_cx2SipRspCodeMap(resultCode);
	if(*rspCode != SIP_RESPONSE_200)
	{
		logInfo("saa resultCode is not 2xxx for pRegInfo(%p).", pRegInfo);
		goto EXIT;
	}

	/* sessSar was initiated when there is no regInfo.  If during the SAR a user tried to register, and regInfo was created
       (even though SAR for the register may still on the way), reject the sessSar to avoid collision.
     */
	status = scscfReg_createSubHash(pRegInfo, true);
	if(status != OS_STATUS_OK)
	{
		logInfo("a UE registration is on the way, abort the UNREGISTERED session.");
		*rspCode = SIP_RESPONSE_500;
		goto EXIT;
	}
	
EXIT:
	//no need to free pDiaDecoded, as this function is the the entry callback by the diameter module

	if(status != OS_STATUS_OK)
	{
		pRegInfo = osfree(pRegInfo);
	}
	else if(pRegInfo)
	{
		*pSIfcIdList = pRegInfo->userProfile.sIfcIdList;
	}

	return pRegInfo;
}


static osStatus_e scscfIfc_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode)
{
    osStatus_e status = OS_STATUS_OK;

    switch(pDiaDecoded->cmdCode)
    {
        case DIA_CMD_CODE_SAR:
            if(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST)
            {
                logError("received SAR request, ignore.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            status = scscfReg_decodeSaa(pDiaDecoded, &pRegInfo->userProfile, pResultCode, &pRegInfo->hssChgInfo);
            if(status == OS_STATUS_OK && cscf_cx2SipRspCodeMap(*pResultCode) == SIP_RESPONSE_200)
            {
                pRegInfo->ueList[0].isImpi = true;
                pRegInfo->ueList[0].impi = pRegInfo->userProfile.impi;
                for(int i=0; i<pRegInfo->userProfile.impuNum; i++)
                {
                    pRegInfo->ueList[i+1].isImpi = false;
                    pRegInfo->ueList[i+1].impuInfo = pRegInfo->userProfile.impuInfo[i];
                }
                pRegInfo->regInfoUENum = pRegInfo->userProfile.impuNum + 1;
            }
            break;
        default:
            logError("unexpected dia command(%d) received, ignore.", pDiaDecoded->cmdCode);
            break;
    }

EXIT:
    return status;
}


void* scscfReg_getRegInfo(osPointerLen_t* pImpu, scscfRegState_e* pRegState, sIfcIdList_t* pSIfcIdList)
{
	scscfRegInfo_t* pRegInfo = osPlHash_getUserData(gScscfRegHash, pImpu, true);
	if(pRegInfo)
	{
		*pRegState = pRegInfo->regState;
		*pSIfcIdList = pRegInfo->userProfile.sIfcIdList;
	}

	mdebug(LM_CSCF, "impu(%r) has pRegInfo(%p).", pImpu, pRegInfo);
	return pRegInfo;
}
	
#if 0
osPointerLen_t* scscfReg_getNoBarImpu(scscfRegIdentity_t ueList[], uint8_t ueNum, bool isTelPreferred)
{
	osPointerLen_t* pImpu = NULL;

	for(int i=0; i<ueNum; i++)
	{
		if(!ueList[i].isImpi && !ueList[i].impuInfo.isBarred)
		{
			if(isTelPreferred)
			{
				if(ueList[i].impuInfo.impu.p[0] == 't' || ueList[i].impuInfo.impu.p[0] == 'T')
				{
					pImpu = &ueList[i].impuInfo.impu;
					goto EXIT;
				}
				else if(!pImpu)
				{
					pImpu = &ueList[i].impuInfo.impu;
				}
			}
			else
			{
				pImpu = &ueList[i].impuInfo.impu;
				goto EXIT;
			}
		}
	}

EXIT:
	if (!pImpu)
	{
		logError("fails to find nobarred impu.");
	}
	return pImpu;
}
				

//find the first barred user
osPointerLen_t* scscfReg_getAnyBarredUser(void* pRegId, osPointerLen_t user[], int userNum)
{
	if(!pRegId || !user)
	{
		return NULL;
	}

	scscfRegInfo_t* pRegInfo = pRegId;
    uint8_t regInfoUENum;
    scscfRegIdentity_t ueList[SCSCF_MAX_ALLOWED_IMPU_NUM+1];
	for(int i=0; i<userNum; i++)
	{
		for(int j=0; j<pRegInfo->regInfoUENum; j++)
		{
			if(pRegInfo->ueList[j].isImpi)
			{
				continue;
			}
			if(osPL_cmp(&user[i], &pRegInfo->ueList[j].impuInfo.impu) == 0)
			{
				if(pRegInfo->ueList[j].impuInfo.isBarred)
				{
					return &pRegInfo->ueList[j].impuInfo.impu;
				}

				break;
			}
		}
	}

	return NULL;
}


bool scscfReg_isUserBarred(void* pRegId, osPointerLen_t* pUser)
{
	bool isBarred = false;

    if(!pRegId || !pUser)
    {
        isBarred = true;
		goto EXIT;
    }

	scscfRegInfo_t* pRegInfo = pRegId;
    uint8_t regInfoUENum;
    scscfRegIdentity_t ueList[SCSCF_MAX_ALLOWED_IMPU_NUM+1];
	for(int j=0; j<pRegInfo->regInfoUENum; j++)
    {
		if(pRegInfo->ueList[j].isImpi)
        {
            continue;
        }

        if(osPL_cmp(pUser, &pRegInfo->ueList[j].impuInfo.impu) == 0)
        {
        	if(pRegInfo->ueList[j].impuInfo.isBarred)
            {
				isBarred = true;
			}

			break;
		}
	}

EXIT:
	return isBarred;
}
#endif

osStatus_e scscfReg_getUeContact(void* pRegId, sipTuAddr_t* pNextHop)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pRegId || !pNextHop)
	{
		logError("null pointer, pRegId=%p, pNextHop=%p.", pRegId, pNextHop);
		goto EXIT;
	}

	scscfRegInfo_t* pRegInfo = pRegId;
	sipHdrMultiContact_t* pContact = pRegInfo->ueContactInfo.regContact.decodedHdr;
	if(!pContact)
	{
		logError("pContact is null.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pContact->isStar)
    {
        logError("contact contains star.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	sipUri_t* pContactUri = &pContact->contactList.pGNP->hdrValue.uri;
	pNextHop->ipPort.ip = pContactUri->hostport.host;
	pNextHop->ipPort.port = pContactUri->hostport.portValue;
	pNextHop->isSockAddr = false;
    pNextHop->tpType = TRANSPORT_TYPE_ANY;
	if(pContactUri->uriParam.uriParamMask & (1<<SIP_URI_PARAM_TRANSPORT))
	{
		if(osPL_strcasecmp(&pContactUri->uriParam.transport, "tcp") == 0)
		{
			pNextHop->tpType = TRANSPORT_TYPE_TCP;
		}
		else if(osPL_strcasecmp(&pContactUri->uriParam.transport, "udp") == 0)
		{
			pNextHop->tpType = TRANSPORT_TYPE_UDP;
		}
	}

EXIT:
	return status;
}
		

//isAllowSameId: true: if the same imsi/impu already in the hash, replace the old one.  false: not allow and return error
osStatus_e scscfReg_createSubHash(scscfRegInfo_t* pRegInfo, bool isAllowSameId)
{
	osStatus_e status = OS_STATUS_OK;

	if(!isAllowSameId)
	{
		for(int i=0; i<pRegInfo->regInfoUENum; i++)
	    {
			osPointerLen_t* pId = pRegInfo->ueList[i].isImpi ? &pRegInfo->ueList[i].impi : &pRegInfo->ueList[i].impuInfo.impu;
			if(osPlHash_getElement(gScscfRegHash, pId, true))
			{
				status =OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
		}
	}

	//isAllowSameId has been checked, no need to worry about isAllowSameId anymore
	for(int i=0; i<pRegInfo->regInfoUENum; i++)
	{
		osPointerLen_t* pId = pRegInfo->ueList[i].isImpi ? &pRegInfo->ueList[i].impi : &pRegInfo->ueList[i].impuInfo.impu;
        osListElement_t* pHashLE = osPlHash_getElement(gScscfRegHash, pId, true);
        if(pHashLE)
        {
            scscfRegInfo_t* pOldRegInfo = osHash_replaceUserData(gScscfRegHash, pHashLE, osmemref(pRegInfo));
            osfree(pOldRegInfo);
        }
        else
        {
            pHashLE = osPlHash_addUserData(gScscfRegHash, pId, true, osmemref(pRegInfo));
scscfRegInfo_t* pTestRegInfo = osPlHash_getUserData(gScscfRegHash, pId, true);
        }

		pRegInfo->ueList[i].pRegHashLE = pHashLE;
	}

EXIT:
	return status;
}

	
//delete hash data for the whole subscription of a UE
void scscfReg_deleteSubHash(scscfRegInfo_t* pRegInfo)
{
	//go through all ID list, and delete them.
	if(pRegInfo->regInfoUENum == 0)
	{
		if(pRegInfo->tempWorkInfo.pRegHashLE)
		{
        	osHash_deleteNode(pRegInfo->tempWorkInfo.pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
		}
	}
	else
	{
		for(int i=0; i<pRegInfo->regInfoUENum; i++)
		{
    		osHash_deleteNode(pRegInfo->ueList[i].pRegHashLE, OS_HASH_DEL_NODE_TYPE_ALL);
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

	if(pRegInfo->expiryTimerId)
	{
		pRegInfo->expiryTimerId = osStopTimer(pRegInfo->expiryTimerId);
	}

	if(pRegInfo->purgeTimerId)
	{
		pRegInfo->purgeTimerId = osStopTimer(pRegInfo->purgeTimerId);
	}

    pRegInfo->regState = SCSCF_REG_STATE_NOT_REGISTERED;
    osList_delete(&pRegInfo->asRegInfoList);
	sipHdrDecoded_cleanup(&pRegInfo->ueContactInfo.regContact);

	osMBuf_reset(&pRegInfo->userProfile.rawUserProfile);
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
