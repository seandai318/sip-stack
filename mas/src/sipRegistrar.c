//for sipRegistrar, accepts ALL SIP REGISTER, does not do any subscription check, or authentication check.  Basically, a IMS AS registratar functionality

#include "osHash.h"
#include "osTimer.h"

#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipRegistrar.h"
#include "masConfig.h"


static void masRegistrar_cleanup(void* pData);
static osStatus_e masReg_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData);
static void masHashData_cleanup(void* data);
static uint64_t masRegStartTimer(time_t msec, void* pData);
static void masReg_onTimeout(uint64_t timerId, void* data);

static osHash_t* masRegHash;
static osListApply_h appInfoMatchHandler;


osStatus_e masReg_init(uint32_t bucketSize, osListApply_h applyHandler)
{
    osStatus_e status = OS_STATUS_OK;

    masRegHash = osHash_create(bucketSize);
    if(!masRegHash)
    {
        logError("fails to create masRegHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	appInfoMatchHandler = applyHandler;

EXIT:
    return status;
}


osStatus_e masReg_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
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
    sipHdrDecoded_t contactHdr={};
    osPointerLen_t* pContactExpire = NULL;

	//raw parse the whole sip message 
	sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
    	logError("fails to sipDecodeMsgRawHdr.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//get the user's sip URI
	osPointerLen_t sipUri;
	debug("sean-remove, from-value=%r", &pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value);
	status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value, &sipUri);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipParamUri_getUriFromRawHdrValue.");
		rspCode = SIP_RESPONSE_503;
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//decode the 1st contact entry
	logError("sean-remove, before decode SIP_HDR_CONTACT (%d).", SIP_HDR_CONTACT);
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, &contactHdr, false);
    if(status != OS_STATUS_OK)
    {
        logError("fails to decode contact hdr in sipDecodeHdr.");
        rspCode = SIP_RESPONSE_400;
        goto EXIT;
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
        osMem_deref(expiryHdr.decodedHdr);
		isExpiresFound = true;
	}
	else
	{
    	osPointerLen_t expireName={"expires", 7};
    	pContactExpire = sipHdrGenericNameParam_getGPValue(&((sipHdrMultiContact_t*)contactHdr.decodedHdr)->contactList.pGNP->hdrValue, &expireName);
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

	if(regExpire != 0 && regExpire < SIP_REG_MIN_EXPIRE)
	{
		regExpire = SIP_REG_MIN_EXPIRE;
		rspCode = SIP_RESPONSE_423;
		goto EXIT;
	}
	else if (regExpire > SIP_REG_MAX_EXPIRE)
	{
		regExpire = SIP_REG_MAX_EXPIRE;
		if(pContactExpire)
		{
			osPL_modifyu32(pContactExpire, regExpire);
		}
	}

	uint32_t key = osHash_getKeyPL(&sipUri, true);
    osListElement_t* pHashLE = osHash_lookupByKey(masRegHash, &key, OSHASHKEY_INT);
    if(!pHashLE)
    {
		if(regExpire == 0)
		{
			rspCode = SIP_RESPONSE_200;
			goto EXIT;
		}

    	osHashData_t* pHashData = osMem_zalloc(sizeof(osHashData_t), masHashData_cleanup);
	    if(!pHashData)
        {
            logError("fails to allocate pHashData.");
			rspCode = SIP_RESPONSE_500;
       	    status = OS_ERROR_MEMORY_ALLOC_FAILURE;
           	goto EXIT;
       	}

		tuRegistrar_t* pRegData = osMem_zalloc(sizeof(tuRegistrar_t), masRegistrar_cleanup);
		osPL_dup(&pRegData->user, &sipUri);
		pRegData->regState = MAS_REGSTATE_REGISTERED;
		sipHdrDecoded_dup(&pRegData->contact, &contactHdr);

		pHashData->hashKeyType = OSHASHKEY_INT;
		pHashData->hashKeyInt = key;
		pHashData->pData = pRegData;
		pRegData->pRegHashLE = osHash_add(masRegHash, pHashData);
				
		pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
			
		rspCode = SIP_RESPONSE_200;
		goto EXIT;
    }

	tuRegistrar_t* pRegData = ((osHashData_t*)pHashLE->data)->pData;

	//for deregistration
	if(regExpire == 0)
	{
	    masReg_deRegUser(TU_DEREG_CAUSE_USER_DEREG, pRegData);

		rspCode = SIP_RESPONSE_200;
		goto EXIT;
	}

	//for registration
	//to-do, compare the contact between new reg and what is in pRegData.  If difference, may need to do auth, etc.  for now, just replace
	sipHdrDecoded_delete(&pRegData->contact);
	sipHdrDecoded_dup(&pRegData->contact, &contactHdr);
	if(pRegData->regState == MAS_REGSTATE_REGISTERED)
	{
		osStopTimer(pRegData->expiryTimerId);
		pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
	}
	else
	{
		osStopTimer(pRegData->purgeTimerId);
		pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
	}

	rspCode = SIP_RESPONSE_200;

EXIT:
	{
		osMBuf_t* pSipResp;
    	sipHdrName_e sipHdrArray[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
    	int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);

		switch(rspCode)
		{
			case SIP_RESPONSE_423:
            	pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
				status = sipTU_addMsgHdr(pSipResp, SIP_HDR_MIN_EXPIRES, &regExpire, NULL);
				status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
				break;
			case SIP_RESPONSE_200:
				pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
				status = sipTU_addContactHdr(pSipResp, pReqDecodedRaw, regExpire);
				status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
				break;
			default:
				pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, true);
				break;
		}

		if(pSipResp)
		{
        	sipTransMsg_t sipTransMsg = {};
        	sipTransMsg.sipMsgType = SIP_MSG_RESPONSE;
        	sipTransMsg.sipMsgBuf.pSipMsg = pSipResp;
        	sipTransMsg.pTransId = pSipTUMsg->pTransId;
			sipTransMsg.rspCode = rspCode;

        	status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);
    	}
    	else
    	{
        	logError("fails to sipTU_buildResponse.");
        	status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		}
		
		if(pContactExpire)
		{
			osMem_deref(pContactExpire);
		}
	}

	DEBUG_END
	return status;
}


static void masReg_onTimeout(uint64_t timerId, void* data)
{ 
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
		masRegistrar_cleanup(pRegData);
	}
	else
	{
		logError("received a unrecognized tiemrId.");
	}

EXIT:
	return;
}


void* masReg_addAppInfo(osPointerLen_t* pSipUri, masInfo_t* pMasInfo)
{
	if(!pSipUri || !pMasInfo)
	{
		logError("null pointyer, pSipUri=%p, pMasInfo=%p.", pSipUri, pMasInfo);
		return NULL;
	}

    uint32_t key = osHash_getKeyPL(pSipUri, true);
    osListElement_t* pHashLE = osHash_lookupByKey(masRegHash, &key, OSHASHKEY_INT);
    if(!pHashLE)
    {
        osHashData_t* pHashData = osMem_zalloc(sizeof(osHashData_t), masHashData_cleanup);
        if(!pHashData)
        {
            logError("fails to allocate pHashData.");
            goto EXIT;
        }

        tuRegistrar_t* pRegData = osMem_zalloc(sizeof(tuRegistrar_t), masRegistrar_cleanup);
        osPL_dup(&pRegData->user, pSipUri);
        pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;
		pMasInfo->regId = pHashLE;
		osList_append(&pRegData->appInfoList, pMasInfo);

        pHashData->hashKeyType = OSHASHKEY_INT;
        pHashData->hashKeyInt = key;
        pHashData->pData = pRegData;
        pRegData->pRegHashLE = osHash_add(masRegHash, pHashData);

        pRegData->purgeTimerId = masRegStartTimer(MAS_REG_PURGE_TIMER*1000, pRegData);
    }
	else
	{
		pMasInfo->regId = pHashLE;
		tuRegistrar_t* pRegData = pHashLE->data;
		osList_append(&pRegData->appInfoList, pMasInfo);
	}

EXIT:	
	return pMasInfo;
}
	

osStatus_e masReg_deleteAppInfo(void* pTUId, void* pTransId)
{
	if(!pTUId || !pTransId)
	{
		logError("null pointer, pTUId=%p, pTransId=%p.", pTUId, pTransId);
		return OS_ERROR_NULL_POINTER;
	}

	osStatus_e status = OS_STATUS_OK;

	tuRegistrar_t* pRegData = ((osListElement_t*)pTUId)->data;
	if(!pRegData)
	{
		logError("pRegData is NULL.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	void* pInfo = osList_deleteElement(&pRegData->appInfoList, appInfoMatchHandler, pTransId);
	if(!pInfo)
	{
		logInfo("fails to find appInfo in osList_deleteElement.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osMem_deref(pInfo);

EXIT:
	return status;
}


void* masReg_getTransId(void* pTUId, void* pTransId, bool isSrcTransId)
{
	if(!pTUId)
	{
		return NULL;
	}

	if((isSrcTransId && ((masInfo_t*)pTUId)->pSrcTransId == pTransId))
	{
		return ((masInfo_t*)pTUId)->pDstTransId;
	}
	else if (!isSrcTransId && ((masInfo_t*)pTUId)->pDstTransId != pTransId)
	{
		return ((masInfo_t*)pTUId)->pSrcTransId;
	}

	return NULL;
}
	

static uint64_t masRegStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masReg_onTimeout, pData);
}


static osStatus_e masReg_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pRegData)
    {
        logError("null pointer, pRegData.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pRegData->regState = MAS_REGSTATE_REGISTERED)
	{
    	pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;
    	osStopTimer(pRegData->expiryTimerId);
    	pRegData->purgeTimerId = masRegStartTimer(MAS_REG_PURGE_TIMER*1000, pRegData);
	}

EXIT:
	return status;
}

	
static void masRegistrar_cleanup(void* pData)
{
	if(!pData)
	{
		return;
	}

	tuRegistrar_t* pRegData = pData;

	if(pRegData->expiryTimerId != 0)
    {
        osStopTimer(pRegData->expiryTimerId);
    }

	if(pRegData->purgeTimerId != 0)
	{
		osStopTimer(pRegData->purgeTimerId);
	}

	osMem_deref(pRegData->contact.decodedHdr);
	osMem_deref(pRegData->contact.rawHdr.buf);
	osPL_dealloc(&pRegData->user);
}

static void masHashData_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	osHashData_t* pHashData = data;
	osMem_deref(pHashData->pData);
}
