//for sipRegistrar, accepts ALL SIP REGISTER, does not do any subscription check, or authentication check.  Basically, a IMS AS registratar functionality

#include "osHash.h"
#include "osTimer.h"

#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"
#include "sipHdrVia.h"

#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipRegistrar.h"



static void masRegistrar_cleanup(void* pData);
static osStatus_e masReg_deRegUser(tuDeRegCause_e deregCause, tuRegistrar_t* pRegData);
static void masHashData_cleanup(void* data);
static uint64_t masRegStartTimer(time_t msec, void* pData);
static void masReg_onTimeout(uint64_t timerId, void* data);
static void masRegData_forceDelete(tuRegistrar_t* pRegData);
static osStatus_e masReg_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static osStatus_e masReg_onTrFailure(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
static void sipReg_onRegister(osPointerLen_t* user);

static osHash_t* masRegHash;
static osListApply_h appInfoMatchHandler;
static sipRegAction_t appRegActionData;


osStatus_e masReg_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    masRegHash = osHash_create(bucketSize);
    if(!masRegHash)
    {
        logError("fails to create masRegHash.");
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


static osStatus_e masReg_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    sipHdrDecoded_t* pContactHdr=osMem_zalloc(sizeof(sipHdrDecoded_t), sipHdrDecoded_cleanup);
//	sipRawHdrListSA_t contactRawHdrSA={};
    sipHdrDecoded_t viaHdr={};
	
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
//	status = sipRawHdr_dup(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT], &contactRawHdrSA);
//    status = sipDecodeHdr(contactRawHdrSA.rawHdr.pRawHdr, pContactHdr, false);
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, pContactHdr, true);
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
		//pContactExpire is not allocated an new memory, it just refer to a already allocated memory in pGNP->hdrValue, no need to dealloc memory for pContactExpire
    	pContactExpire = sipHdrGenericNameParam_getGPValue(&((sipHdrMultiContact_t*)pContactHdr->decodedHdr)->contactList.pGNP->hdrValue, &expireName);
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
			logInfo("received a out of blue deregister message, sipuri=%r", &sipUri);
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

		pRegData = osMem_zalloc(sizeof(tuRegistrar_t), masRegistrar_cleanup);
		osDPL_dup(&pRegData->user, &sipUri);
//		pRegData->regState = MAS_REGSTATE_REGISTERED;
//		pRegData->pContact = osMem_ref(pContactHdr);
//		sipHdrDecoded_dup(&pRegData->pContact-> &contactHdr);

		pHashData->hashKeyType = OSHASHKEY_INT;
		pHashData->hashKeyInt = key;
		pHashData->pData = pRegData;
logError("to-remvoe, just to check the creation of a address.");
		pRegData->pRegHashLE = osHash_add(masRegHash, pHashData);
		logInfo("user(%r) is add into masRegHash, key=0x%x, pRegHashLE=%p", &sipUri, key, pRegData->pRegHashLE);
        logError("to-remove, regState, pRegHashLE=%p, pRegData=%p, state=%d, MAS_REGSTATE_REGISTERED=%d", pRegData->pRegHashLE, pRegData, pRegData->regState, MAS_REGSTATE_REGISTERED);

		pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;				
//		pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
			
		rspCode = SIP_RESPONSE_200;
		goto EXIT;
    }

	pRegData = ((osHashData_t*)pHashLE->data)->pData;

	rspCode = SIP_RESPONSE_200;

EXIT:
	{
		sipHostport_t peer;
		peer.host = pSipTUMsg->pPeer->ip;
		peer.portValue = pSipTUMsg->pPeer->port;

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
				status = sipTU_msgBuildEnd(pSipResp, false);
				break;
			case SIP_RESPONSE_INVALID:
				//do nothing here, since pSipResp=NULL, the implementation will be notified to abort the transaction
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

		if(pSipResp)
		{
logError("to-remove, masReg, pRegData=%p", pRegData);
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
			sipTransMsg.isTpDirect = false;
        	sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
        	sipTransMsg.pTransId = pSipTUMsg->pTransId;
			sipTransMsg.appType = SIPTU_APP_TYPE_REG;
			sipTransMsg.response.rspCode = rspCode;
			sipTransMsg.pSenderId = pRegData;

        	status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);
			
			//masRegistrar does not need to keep pSipResp, if other layers need it, it is expected they will ref it
			osMem_deref(pSipResp);

			//handle corner error case when the sending of rsp wa failed, and for normal case, start timer, set reg state, etc.
			if(rspCode == SIP_RESPONSE_200 && pRegData)
			{
				if(pRegData->isRspFailed)
				{
					//handle the case when a UE is a initial registration, for re-reg, let it time out
					if(pRegData->regState != MAS_REGSTATE_REGISTERED)
					{
		        		masRegData_forceDelete(pRegData);
					}
				}
				else
				{
					if(regExpire == 0)
		        	{
						//if dereg procedure has already started, do nothing
						if(!pRegData->purgeTimerId)
						{
							masReg_deRegUser(TU_DEREG_CAUSE_USER_DEREG, pRegData);
						}
					}
					else
					{
				    	if(pRegData->regState == MAS_REGSTATE_REGISTERED)
    					{
    						osMem_deref(pRegData->pContact);
    						pRegData->pContact = osMem_ref(pContactHdr);

        					osStopTimer(pRegData->expiryTimerId);
        					pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
    					}
    					else
    					{
					        pRegData->pContact = osMem_ref(pContactHdr);

        					pRegData->regState = MAS_REGSTATE_REGISTERED;
							if(pRegData->purgeTimerId)
							{
        						osStopTimer(pRegData->purgeTimerId);
								pRegData->purgeTimerId = 0;
							}
        					pRegData->expiryTimerId = masRegStartTimer(regExpire*1000, pRegData);
							//start timer to query DB if there is stored SMS for the user, the reason not to query right away is that client could not handle immediate SMS after register
							pRegData->smsQueryTimerId = masRegStartTimer(5000, pRegData);
    					}
					}
				}
			}

			pRegData ? pRegData->isRspFailed = false : (void)0;

    	}
    	else
    	{
        	logError("fails to sipTU_buildResponse.");
			//to-do need to notify transaction layer to drop the transaction
        	status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		}
	}

//	osMem_deref(pContactHdr->decodedHdr);
logError("to-remove, viaHdr.decodedHdr=%p, pContactHdr=%p", viaHdr.decodedHdr, pContactHdr);
	osMem_deref(viaHdr.decodedHdr);	
	osMem_deref(pContactHdr);

//logError("to-remove, masreg1, pRegData=%p, peer-ipaddr=%p, peer=%r:%d", pRegData, ((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.host.p, &((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.host, ((sipHdrMultiContact_t*)pRegData->pContact->decodedHdr)->contactList.pGNP->hdrValue.uri.hostport.portValue);

	DEBUG_END
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
		osMem_deref(pRegData);
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
	

void* masReg_addAppInfo(osPointerLen_t* pSipUri, void* pMasInfo)
{
	DEBUG_BEGIN

	if(!pSipUri || !pMasInfo)
	{
		logError("null pointyer, pSipUri=%p, pMasInfo=%p.", pSipUri, pMasInfo);
		return NULL;
	}

	void* pRegId = NULL;
//	pMasInfo->regId = NULL;

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
        osDPL_dup(&pRegData->user, pSipUri);
        pRegData->regState = MAS_REGSTATE_NOT_REGISTERED;
        logError("to-remove, regState, pRegData=%p, state=%d, MAS_REGSTATE_REGISTERED=%d", pRegData, pRegData->regState, MAS_REGSTATE_REGISTERED);
#if 0
//since SMS is not an session, really there is no need to store sms in regData 
		osList_append(&pRegData->appInfoList, pMasInfo);
#endif
        pHashData->hashKeyType = OSHASHKEY_INT;
        pHashData->hashKeyInt = key;
        pHashData->pData = pRegData;
        pRegData->pRegHashLE = osHash_add(masRegHash, pHashData);
//		pMasInfo->regId = pRegData->pRegHashLE;
		pRegId = pRegData->pRegHashLE;
logError("to-remove, CRASH, pMasInfo=%p, pRegId=%p", pMasInfo, pRegId); 
	
        pRegData->purgeTimerId = masRegStartTimer(SIP_REG_PURGE_TIMER*1000, pRegData);
    }
	else
	{
//		pMasInfo->regId = pHashLE;
		pRegId = pHashLE;
		tuRegistrar_t* pRegData = ((osHashData_t*)pHashLE->data)->pData;
#if 0
//since SMS is not an session, really there is no need to store sms in regData
		osList_append(&pRegData->appInfoList, pMasInfo);
#endif
logError("to-remove, CRASH, pRegData=%p, pMasInfo(pInfo)=%p, pHashLE=%p", pRegData, pMasInfo, pHashLE);
	}

EXIT:	
	DEBUG_END
	return pRegId;
}
	

osStatus_e masReg_deleteAppInfo(void* pRegId, void* pTransId)
{
	if(!pRegId || !pTransId)
	{
		logError("null pointer, pRegId=%p, pTransId=%p.", pRegId, pTransId);
		return OS_ERROR_NULL_POINTER;
	}

logError("to-remove, CRASH, pRegId=%p", pRegId);
	osStatus_e status = OS_STATUS_OK;
	tuRegistrar_t* pRegData = osHash_getData(pRegId);
	if(!pRegData)
	{
		logError("pRegData is NULL.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

logError("to-remove, pRegData=%p, pTransId=%p", pRegData, pTransId);
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


#if 0
//move to masMgr, and change name to masInfo_getTransId()
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
#endif	


static uint64_t masRegStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masReg_onTimeout, pData);
}


static void sipReg_onRegister(osPointerLen_t* user)
{
	if(appRegActionData.appAction)
	{
		appRegActionData.appAction(user, appRegActionData.pAppData);
	}
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

	//to-do, update so that just call one function, osMem_deref(pRegData->pContact) will clean the remaining
//	osMem_deref(pRegData->pContact->decodedHdr);
//	osMem_deref(pRegData->pContact->rawHdr.buf);
    osMem_deref(pRegData->pContact);

	osDPL_dealloc(&pRegData->user);
	osList_delete(&pRegData->appInfoList);

	//remove from hash
	if(pRegData->pRegHashLE)
	{
		logInfo("delete hash element, key=%ud", ((osHashData_t*)pRegData->pRegHashLE->data)->hashKeyInt);
		osHash_deleteNode(pRegData->pRegHashLE);
	}
	osfree(pRegData->pRegHashLE);
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

    osMem_deref(pRegData);
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
