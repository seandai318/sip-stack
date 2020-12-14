/********************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file icscfRegistrar.c
 * implement 3GPP 24.229 ICSCF registration. 
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
#include "diaCxAvp.h"

#include "scscfRegistrar.h"
#include "cscfConfig.h"
#include "scscfCx.h"
#include "cscfHelper.h"
#include "scscfIfc.h"
#include "scscfIntf.h"



static osStatus_e icscfReg_onSipRequest(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osStatus_e icscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isViaIcscf);
static osStatus_e icscfReg_forwardSipRegister(scscfRegInfo_t* pRegInfo, sipTuAddr_t* pNextHop);
static bool icscfReg_getNextScscfByCap(diaCxServerCap_t* pServerCap, osListPlusElement_t* pCurPlusLE, sipTuAddr_t* pScscfAddr, bool* isLocal);
static void icscfUaaInfo_cleanup(void* data);
static void icscfRegInfo_cleanup(void* data);



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



static osHash_t* gIcscfRegHash;



osStatus_e icscfReg_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    gIcscfRegHash = osHash_create(bucketSize);
    if(!gIcscfRegHash)
    {
        logError("fails to create gIcscfRegHash, bucketSize=%u.", bucketSize);
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    sipTU_attach(SIPTU_APP_TYPE_ICSCF_REG, icscfReg_onTUMsg);

EXIT:
    return status;
}


//the entering point for icscfReg to receive SIP messages from the transaction layer
osStatus_e icscfReg_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	switch(msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			if(pSipTUMsg->sipMsgType == SIP_MSG_REQUEST)
			{
				return icscfReg_onSipRequest(msgType, pSipTUMsg);
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
			icscfReg_onTrFailure(msgType, pSipTUMsg);
			break;
		default:
			logError("msgType(%d) is not handled.", msgType);
			break;
	}

	return OS_ERROR_INVALID_VALUE;
}


static osStatus_e icscfReg_onSipRequest(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;

    if(!pSipTUMsg)
    {
        logError("null pointer, pSipTUMsg.");
        return;
    }

	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
	if(!pReqDecodedRaw)
	{
        logError("fails to sipDecodeMsgRawHdr.");
       	status = OS_ERROR_EXT_INVALID_VALUE;
       	goto EXIT;
	}

	osPointerLen_t impu;
    if(sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_TO]->pRawHdr->value, &impu) != OS_STATUS_OK)
    {
        logError("fails to sipParamUri_getUriFromRawHdrValue.");
        rspCode = SIP_RESPONSE_400;
		status = OS_ERROR_EXT_INVALID_VALUE;
        goto EXIT;
    }

	osPointerLen_t impi;
	if(cscf_getImpiFromSipMsg(pReqDecodedRaw, &impu, &impi) != OS_STATUS_OK)
	{
		logError("fails to cscf_getImpiFromSipMsg.");
		rspCode = SIP_RESPONSE_400;
        status = OS_ERROR_EXT_INVALID_VALUE;
		goto EXIT;
	}

	status = icscfReg_processRegMsg(&impi, &impu, pSipTUMsg, pReqDecodedRaw);

EXIT:
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		cscf_sendRegResponse(pSipTUMsg, pReqDecodedRaw, NULL, 0, pSipTUMsg->pPeer, pSipTUMsg->pLocal, rspCode);
	}

	return status;
}


typedef struct {
    DiaCxUarAuthType_e authType;
	diaResultCode_t rspCode;
	bool isCap;		//=true, use serverCap, otherwise, use serverName
	bool isCapExist;	//if UAA has CAP avp.  note it is possible that isCap=true & isCapExist=false, meaning the UAA does not have server name neither CAP avp
	osListPlusElement_t curLE; //the LE that stores the current cap in serverCap, only valid when isCap = true and isCapExist = true
	osPointerLen_t serverName;
	diaCxServerCap_t serverCap;	//for now, only support using cap value, not support using server name inside this avp
    diaMsgDecoded_t* pMsgDecoded;
} icscfUaaInfo_t;


typedef struct {
	osPointerLen_t impi;
	osPointerLen_t impu;
    sipTUMsg_t* pSipTUMsg;
	sipMsgDecodedRawHdr_t* pReqDecodedRaw;
	icscfUaaInfo_t uaaInfo;
} icscfRegInfo_t;

//common function for messages both from transaction or from icscf
static osStatus_e icscfReg_processRegMsg(osPointerLen_t* pImpi, osPointerLen_t* pImpu, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw)
{
    osStatus_e status = OS_STATUS_OK;

	icscfRegInfo_t* pRegInfo = osmalloc(sizeof(icscfRegInfo_t), icscfRegInfo_cleanup);
	pRegInfo->pSipTUMsg = pSipTUMsg;
	pRegInfo->pReqDecodedRaw = pReqDecodedRaw;
	pRegInfo->impi = *pImpi;
	pRegInfo->impu = *pImpu;

    sipResponse_e rspCode = SIP_RESPONSE_INVALID;
    osPointerLen_t* pContactExpire = NULL;
    scscfRegInfo_t* pRegInfo = NULL;

    //check the expire header
    uint32_t regExpire = 0;
	bool isExpireFound = sipTu_getRegExpireFromMsg(pReqDecodedRaw, &regExpire, NULL, &rspCode);
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		goto EXIT;
	}

	if(isExpireFound && regExpire == 0)
	{
		pRegInfo->uaaInfo.authType = DIA_CX_AUTH_TYPE_REGISTRATION;
	}
	else
	{
		pRegInfo->uaaInfo.authType = ICSCF_UAR_AUTHTYPE_CAPABILITY ? DIA_CX_AUTH_TYPE_REGISTRATION_AND_CAPABILITIES : DIA_CX_AUTH_TYPE_REGISTRATION;
	}

	//perform UAR
	icscfReg_performUar();

EXIT:
    if(rspCode != SIP_RESPONSE_INVALID)
    {
        cscf_sendRegResponse(pSipTUMsg, pReqDecodedRaw, NULL, 0, pSipTUMsg->pPeer, pSipTUMsg->pLocal, rspCode);
    }

	osfree(pRegInfo);

	return status;
}



//pDiaDecoded = NULL indicates no diameter response is received, like timed out
//need to save HSS msg, to-do
void icscfReg_onDiaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData)
{
	sipResponse_e rspCode = SIP_RESPONSE_INVALID;
 
    if(!pAppData)
    {
        logError("null pointer, pAppData.");
        goto EXIT;
    }

	icscfRegInfo_t* pRegInfo = pAppData;
	if(!pDiaDecoded)
	{
		logError("fails to perform UAR diameter query for impi(%r).", &pRegInfo->impi);
		rspCode = SIP_RESPONSE_500; 
		goto EXIT;
	}

    diaResultCode_t resultCode;
    switch(pDiaDecoded->cmdCode)
    {
        case DIA_CMD_CODE_UAR:
            if(pDiaDecoded->cmdFlag & DIA_CMD_FLAG_REQUEST)
            {
                logError("received UAR request for impi(%r), ignore.", &pRegInfo->impi);
                goto EXIT;
            }

            if(icscfCx_decodeUaa(pDiaDecoded, &pRegInfo->userProfile, &resultCode, &pRegInfo->hssChgInfo) != OS_STATUS_OK)
			{
				logError("fails to decode UAA for impi(%r).", &pRegInfo->impi); 
				rspCode = SIP_RESPONSE_500;
				goto EXIT;
			}

            rspCode = cscf_cx2SipRspCodeMap(resultCode);
			//for good response, reset rspCode so that no response will be sent back
			if(rspCode < 200 || rspCode >= 300)
			{
				rspCode = SIP_RESPONSE_INVALID;
			}
            break;
        default:
            logError("unexpected dia command(%d) received for impi(%r), ignore.", pDiaDecoded->cmdCode, &pRegInfo->impi);
			rspCode = SIP_RESPONSE_500;
            break;
    }

    switch(pDiaDecoded->cmdCode)
    {
        case DIA_CMD_CODE_UAR:
            icscfReg_onUaa(pRegInfo, resultCode);
            break;
        case DIA_CMD_CODE_LIR:
            icscfReg_onLia(pRegInfo);
            break;
        default:
            logError("scscf receives dia cmdCode(%d) that is not supported, ignore.", pDiaDecoded->cmdCode);
            break;
    }

EXIT:
	if(rspCode != SIP_RESPONSE_INVALID)
	{
        cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, rspCode);

		osfree(pRegInfo);
	}
    return;
}


static void icscfReg_onUaa(icscfRegInfo_t* pRegInfo)
{
	osPointerPen_t* pScscf = NULL;
	bool isFreeRegInfo = true;

	if(pRegInfo->uaaInfo.isCap && !pRegInfo->uaaInfo.isCapExist)
	{
		if(pRegInfo->uaaInfo.resultCode == DIA_CX_EXP_RESULT_FIRST_REGISTRATION)
		{
			scscfReg_onIcscfMsg(SIP_MSG_REQUEST, pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, &pRegInfo->impi, &pRegInfo->impu);

			goto EXIT;
		}

		logError("the UAA for impi(%r) does not contain server name and capability, and the result code is not DIA_CX_EXP_RESULT_FIRST_REGISTRATION.", &pRegInfo->impi);
		cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, SIP_RESPONSE_500);
		goto EXIT;
	}

	bool isServerLocal = true;
	sipTuAddr_t nextScscf;
	if(pRegInfo->uaaInfo.isCap)
	{
		if(!icscfReg_getNextScscfByCap(&pRegInfo->uaaInfo.serverCap, &pRegInfo->uaaInfo.curLE, &nextScscf, &isLocal))
		{
			logInfo("no SCSCF available for impi(%r).", &pRegInfo->impi);
			cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, SIP_RESPONSE_500);
			goto EXIT;
		}
	}
	else
	{
		if(!icscfConfig_getScscfInfoByName(&pRegInfo->uaaInfo.serverName, &nextScscf, &isLocal))
        {
            logInfo("no SCSCF available scscf name(%r).", &pRegInfo->uaaInfo.serverName);
            cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, SIP_RESPONSE_500);
            goto EXIT;
        }

		//if the specified server name is quarantined, and the previous UAR query was not for capabilities, try one more query for capabilities
		if(icscf_isScscfQuarantined(nextScscf.sockAddr))
		{
			logInfo("scscf name(%r) is quarantined.", &pRegInfo->uaaInfo.serverName);
			if(pRegInfo->uaaInfo.authType == DIA_CX_AUTH_TYPE_REGISTRATION)
			{
				icscfUaaInfo_cleanup(&pRegInfo->uaaInfo);

				pRegInfo->uaaInfo.authType = DIA_CX_AUTH_TYPE_REGISTRATION_AND_CAPABILITIES;
			    icscfReg_performUar();
			
				isFreeRegInfo = false;
 			}
			else
			{
            	cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, SIP_RESPONSE_500);
			}
            goto EXIT;
        }
	}

	if(isServerLocal)
	{
		scscfReg_onIcscfMsg(SIP_MSG_REQUEST, pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, &pRegInfo->impi, &pRegInfo->impu);
    }
	else
	{
		if(icscfReg_forwardSipRegister(pRegInfo, &nextScscf) != OS_STATUS_OK)
		{
			logInfo("fails to forward register for impi(%r).", &pRegInfo->impi);
            cscf_sendRegResponse(pRegInfo->pSipTUMsg, pRegInfo->pReqDecodedRaw, NULL, 0, pRegInfo->pSipTUMsg->pPeer, pRegInfo->pSipTUMsg->pLocal, SIP_RESPONSE_500);
            goto EXIT;
        }

		isFreeRegInfo = false;
	}
	
EXIT:
	if(isFreeRegInfo)
	{
		osfree(pRegInfo);
	}

	return;
}


//this is when remote SCSCF returns register response.  For register sent to the local scscf, there will be no response back
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


	status = sipProxy_forwardResp(pSipTUMsg, pReqDecodedRaw, pTransId, pCallInfo->pProxyInfo);

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
static osStatus_e icscfReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
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


static osStatus_e icscfReg_forwardSipRegister(scscfRegInfo_t* pRegInfo, sipTuAddr_t* pNextHop)
{
    osStatus_e status = OS_STATUS_OK;

    //build a sip register message towards an AS and forward.  May also to use sipTU_uacBuildRequest() and sipMsgAppendHdrStr(), instead, sipProxy_forwardReq() is chosen.
    int len = 0;
    sipProxy_msgModInfo_t msgModInfo = {false, 0};

    proxyInfo_t* pProxyInfo = oszalloc(sizeof(proxyInfo_t), NULL);
    pProxyInfo->proxyOnMsg = NULL;      //for cscf proxyOnMsg is not used since cscf does not distribute message further after receiving from TU
    pProxyInfo->pCallInfo = pRegInfo;

    sipTuUri_t targetUri = {*pRegInfo->tempWorkInfo.pAs, true};
    void* pTransId = NULL;
    status = sipProxy_forwardReq(pRegInfo->uaaInfo.pTUMsg, pRegInfo->uaaInfo.pReqDecodedRaw, NULL, &msgModInfo, pNextHop, false, pProxyInfo, &pTransId);
    if(status != OS_STATUS_OK || !pTransId)
    {
        logError("fails to forward sip request, status=%d, pTransId=%p.", status, pTransId);
        status = pTransId ? status : OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


//only check mandatory capability
//return value: if the next scscf is found, true, else, false
static bool icscfReg_getNextScscfByCap(diaCxServerCap_t* pServerCap, osListPlusElement_t* pCurPlusLE, sipTuAddr_t* pScscfAddr, bool* isLocal)
{
	if(!pServerCap || !pCurPlusLE || !pScscfAddr || !isLocal)
	{
		logError("null pointer, pServerCap=%p, pCurPlusLE=%p, pScscfAddr=%p, isLocal=%p.", pServerCap, pCurPlusLE, pScscfAddr, isLocal);
		return false;
	}
	
	uint32_t* pCapValue = osListPlus_getNextData(&pCurPlusLE->manCap, pCurPlusLE);
	if(!pCapValue)
	{
		logInfo("no more next SCSCF address.");
		return false;
	}

	while (1)
	{
		if(!cscfConfig_getScscfInfoByCap(*pCapValue, pScscfAddr, isLocal))
		{
			logError("there is no configured scscf for capability(%d).", *pCapValue);
			return false;
		}

		if(!*isLocal && icscf_isScscfQuarantined(pScscfAddr->sockAddr))
		{
			continue;
		}

		break;
	}

	return true;
}


static void icscfUaaInfo_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	icscfUaaInfo_t* pUaaInfo = data;
	pUaaInfo->pMsgDecoded = osfree(pUaaInfo->pMsgDecoded);
	osListPlus_clear(&pUaaInfo->serverCap.manCap);
	osListPlus_clear(&pUaaInfo->serverCap.optCap);
	osListPlus_clear(&pUaaInfo->serverCap.serverName);
	
	pUaaInfo->isCap = false;
	pUaaInfo->isCapExist = false;
}


static void icscfRegInfo_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    icscfRegInfo_t* pRegInfo = data;

	osfree(pRegInfo->pSipTUMsg);
	osfree(pRegInfo->pReqDecodedRaw);
	icscfUaaInfo_cleanup(&pRegInfo->uaaInfo);
}

