/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfSessionMgr.c
 ********************************************************/

#include "osTypes.h"
#include "osHash.h"
#include "osDebug.h"
#include "osPL.h"

#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipHdrMisc.h"
#include "sipTUIntf.h"
#include "proxyMgr.h"
#include "proxyHelper.h"
#include "callProxy.h"


//right now the creation of hash is called from the main program, the proxyHash can not be per thread.  if want per thread hash, need to call the proxy_init() directly from a thread.
static osHash_t* proxyHash;


osStatus_e proxy_init(uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    proxyHash = osHash_create(bucketSize);
    if(!proxyHash)
    {
        logError("fails to create proxyHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	sipTU_attach(SIPTU_APP_TYPE_PROXY, proxy_onSipTUMsg);

	callProxy_init();
	//saProxy_init();

EXIT:
    return status;
}


osStatus_e scscf_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    switch (msgType)
    {
        case SIP_TU_MSG_TYPE_MESSAGE:
		{
		    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    		if(pReqDecodedRaw == NULL)
    		{
        		logError("fails to sipDecodeMsgRawHdr. The received sip message may not be correctly encoded.");
		        sipProxy_uasResponse(SIP_RESPONSE_400, pSipTUMsg, pReqDecodedRaw, pSipTUMsg->pTransId, NULL);
        		status = OS_ERROR_INVALID_VALUE;
        		goto EXIT;
    		}

			//determine if the message is MO or MT, from UE or from AS
			/* 1. check if there is matching user part with an existing session.  Here there are two cases, user part can be a 
			   orig-dialog-id that matches with a existing ongoing session, or a id that scscf put in the record-route.  The first case
			   is for the initial request for a session, the second case is for the subsequent request within a already established
			   session.  Eitherway, if yes, continue the already started call flow, no need to judge MO or MT, even if orig present.  The
			   stored session data will tell which case the request is.
			*/
			sipHdrDecoded_t sipHdrDecoded = {};
			sipHdrGenericNameParam_t* pTopRoute = sipDecodeTopRouteValue(pReqDecodedRaw, &sipHdrDecoded, false);
			if(!pTopRoute)
			{
			    osfree(sipHdrDecoded.decodedHdr);
				//treats as MT initial
			}
			osListElement_t* pHashLE = osPlHash_getElement(proxyHash, &pTopRoute->uri.userInfo.sipUser.user, true);
#if 0
		    if(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE])
    		{
        		sipHdrDecoded_t sipHdrDecoded = {};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE]->pRawHdr, &sipHdrDecoded, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
            goto EXIT;
        }

        sipHdrRoute_t* pRoute = sipHdrDecoded.decodedHdr;
        osPointerLen_t lrName = {"lr", 2};
        pLR = sipParamNV_getValuefromList(&pRoute->pGNP->hdrValue.genericParam, &lrName);
#endif

			if(pHashLE)
			{
				//continue to perform ifc to find next hop
				status = scscf_continueHandling(pSipTUMsg, pReqDecodedRaw, pHashLE);
			}
			else
			{
	            //2. if there is no orig-dialog-id, check the topmost route to see if there is "orig" parameter, if yes, MO, otherwise, MT
				osPointerLen_t paramOrig = {"orig", sizeof("orig")-1};
				if(sipParamNV_getValuefromList(&pTopRoute->genericParam, &paramOrig))
				{
					status = scscfSess_initialOrigHandling(pSipTUMsg, pReqDecodedRaw);
				}
				else
				{
                    status = scscfSess_initialTermHandling(pSipTUMsg, pReqDecodedRaw);
                }
			}

		    osfree(sipHrDecoded.decodedHdr);
            break;
		}
        case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
        default:
            status = ((proxyInfo_t*)pSipTUMsg->pTUId)->proxyOnMsg(SIP_TU_MSG_TYPE_TRANSACTION_ERROR, pSipTUMsg, NULL, NULL);
            break;
    }

EXIT:
    DEBUG_END
    return status;
}


osStatus_e scscfSess_continueHandling(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE)
{
                proxy_onMsgCallback proxyOnMsg = ((proxyInfo_t*)((osHashData_t*)pHashLE->data)->pData)->proxyOnMsg;
                status = proxyOnMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, nextHop, pHashLE);
}


osStatus_e scscfSess_initialOrigHandling(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw)
{
	osStatus_e status = OS_STATUS_OK;

	osPointerLen_t* pImpu = scscfSess_getOrigUser(pSipTUMsg, pReqDecodedRaw);
	if(!pImpu)
	{
		logError("the request does not contain proper served user for the originating service.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	scscfUserProfile_t* pUserProfile = scscfReg_getUserProfile(pImpu);
	if(!pUserProfile)
	{
		mlogInfo(LM_CSCF, "no user profile for impu(%r).", pImpu);
		
		//download user profile from HSS
		scscfSessData_t* pSessData = osmalloc(sizeof(scscfSessData_t), scscfSessData_cleanup);
		pSessData->pSipTUMsg = pSipTUMsg;
		pSessData->pReqDecodedRaw = pReqDecodedRaw;
		pSessData->pImpu = pImpu;
		pSessData->sessState = SCSCF_SESS_STATE_INIT_ORIG;

osStatus_e diaCx_initSAR(osPointerLen_t* pImpi, osPointerLen_t* pImpu, dia3gppServerAssignmentType_e sarType, diaNotifyApp_h appCallback, void* appData)
		status = scscfReg_sessInitSAR(pImpu, DIA_3GPP_CX_UNREGISTERED_USER, pSessData);
		if(status!= OS_STATUS_OK)
		{
			logError("fails to scscfReg_sessInitSAR for impu(%r).", pImpu);	
//		osHashPl_addUserData(proxyHash, &pSessId->pl, true, pSessData);
			goto EXIT;
		}
	}
	
	//for shared ifc, each session needs to add a reference, and remove the reference after is done.  the scscf shared ifc keeper has its own reference.  when user needs to change the shared ifc, the scscf sifc keeper removes its own reference to the old sifc, and points to the new sifc.  The old sifc will eventually be freed after all ongoing session done with it.  All new sessions will use the new ifc as soon as the scscf sifc keeper points to the new sifc.  if the design is sifc is to be shared by multiple threads, Make sure to add a mutex for the sifc switch 	
	osListElement_t* pCurIfcLE = NULL;
	osPointerLen_t* pNextHop = scscf_getIfcNextHop(pUserProfile->pIfc, &pCurIfcLE);
	if(!pNextHop)
	{
		mdebug(LM_CSCF, "no matching ifc entry for impu(%r).", pImpu);
		//need to forward to the MT side.  Do not expect there is route exists except for one pointing to SCSCF (may need to check and verify, to-do later).

		scscfSess_forward2Bgcf();
		goto EXIT;
	}

	if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_INVITE)
	{
		status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pNextHop, NULL);
    }
    else
    {
		//status = saProxy_onSipMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pNextHop, NULL);
    }
}


osStatus_e scscf_initialTermHandling(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw)
{
    osPointerLen_t* pImpu = scscfSess_getTermUser(pSipTUMsg, pReqDecodedRaw);
    if(!pImpu)
    {
        logError("the request does not contain proper served user for the originating service.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    scscfUserProfile_t* pUserProfile = scscfReg_getUserProfile(pImpu);
    if(!pUserProfile)
    {
        mlogInfo(LM_CSCF, "no user profile for impu(%r).", pImpu);

        //download user profile from HSS
        scscfSessData_t* pSessData = osmalloc(sizeof(scscfSessData_t), scscfSessData_cleanup);
        pSessData->pSipTUMsg = pSipTUMsg;
        pSessData->pReqDecodedRaw = pReqDecodedRaw;
        pSessData->pImpu = pImpu;
        pSessData->sessState = SCSCF_SESS_STATE_INIT_TERM;

        osVPointerLen_t* pSessId = scscf_initSAR(pImpu, DIA_3GPP_CX_UNREGISTERED_USER);
        osHashPl_addUserData(proxyHash, &pSessId->pl, true, pSessData);
        goto EXIT;
    }

    osListElement_t* pCurIfcLE = NULL;
    osPointerLen_t* pNextHop = scscf_getIfcNextHop(pUserProfile->pIfc, &pCurIfcLE);
    if(!pNextHop)
    {
        mdebug(LM_CSCF, "no matching ifc entry for impu(%r).", pImpu);
        //forward the request to the UE
        scscfSess_forward2UE();
        goto EXIT;
    }

    if(pSipTUMsg->pSipMsgBuf->reqCode == SIP_METHOD_INVITE)
    {
        status = callProxy_onSipTUMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pNextHop, NULL);
    }
    else
    {
        //status = saProxy_onSipMsg(SIP_TU_MSG_TYPE_MESSAGE, pSipTUMsg, pReqDecodedRaw, pNextHop, NULL);
    }
}


osHash_t* proxy_getHash()
{
	return proxyHash;
}

