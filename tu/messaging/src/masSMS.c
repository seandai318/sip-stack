/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file masSMS.c
 ********************************************************/


#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipHdrVia.h"
#include "sipHeaderMisc.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTransIntf.h"
#include "sipRegistrar.h"

#include "masMgr.h"
#include "masConfig.h"
#include "masDb.h"
#include "masSipHelper.h"


static osStatus_e masSMS_onSipMsg(sipTUMsg_t* pSipTUMsg);
static osStatus_e masSMS_onSipTransError(sipTUMsg_t* pSipTUMsg);
static osStatus_e masSMS_onSipRequest(sipTUMsg_t* pSipTUMsg);
static osStatus_e masSMS_onSipResponse(sipTUMsg_t* pSipTUMsg);
static void masSMS_onTimeout(uint64_t timerId, void* data);


osStatus_e masSMS_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;

	switch (msgType)
	{
		case SIP_TU_MSG_TYPE_MESSAGE:
			status = masSMS_onSipMsg(pSipTUMsg);
			break;
		case SIP_TU_MSG_TYPE_TRANSACTION_ERROR:
		default:
			status =  masSMS_onSipTransError(pSipTUMsg);
			break;
	}

	DEBUG_END
	return status;
}


//for now there is no timeout event for masSMS.  It may be added later when SMPP is hooked
void masSMS_onTimeout(uint64_t timerId, void* data)
{
    return;
}


static osStatus_e masSMS_onSipMsg(sipTUMsg_t* pSipTUMsg)
{
	osStatus_e status = OS_STATUS_OK;

	switch(pSipTUMsg->sipMsgType)
	{
		case SIP_MSG_REQUEST:
			return masSMS_onSipRequest(pSipTUMsg);
			break;
        case SIP_MSG_RESPONSE:
			return masSMS_onSipResponse(pSipTUMsg);
			break;
		default:
			logError("received a unknown msgType(%d).", pSipTUMsg->sipMsgType);
			status = OS_ERROR_INVALID_VALUE;
			break;
	}

	return status;
}


static osStatus_e masSMS_onSipRequest(sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    sipResponse_e rspCode = SIP_RESPONSE_INVALID;

    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(&pSipTUMsg->sipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
        logError("fails to sipDecodeMsgRawHdr.  Since the received SIP message was not decoded, there will not be any sip response.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(pSipTUMsg->sipMsgBuf.reqCode != SIP_METHOD_MESSAGE)
	{
   		logError("receives unexpected sip message type (%d).", pSipTUMsg->sipMsgBuf.reqCode);
		rspCode = SIP_RESPONSE_503;
		status = OS_ERROR_INVALID_VALUE;
		goto BUILD_RESPONSE;
	}

	bool isOrig;
	status = sipTU_asGetSescase(pReqDecodedRaw, &isOrig);

    //do not accept terminating SIP MESSAGE
    if(!isOrig || status != OS_STATUS_OK)
    {
        rspCode = SIP_RESPONSE_503;
        status = OS_ERROR_INVALID_VALUE;
        goto BUILD_RESPONSE;
    }

	//for now, use proxy mode
    //get the user's sip URI
	osPointerLen_t callerUri;
	status = sipTU_asGetUser(pReqDecodedRaw, &callerUri, true, isOrig);
	if(status != OS_STATUS_OK)
	{
   		logError("fails to sipTU_asGetUser.");
   		rspCode = SIP_RESPONSE_500;
   		status = OS_ERROR_INVALID_VALUE;
   		goto BUILD_RESPONSE;
	}

    osPointerLen_t sms={};
    status = masSip_getSms(pReqDecodedRaw, &sms);

	sipUri_t* pCalledContactUser = NULL;
	osPointerLen_t calledUri;
	status = sipTU_asGetUser(pReqDecodedRaw, &calledUri, false, isOrig);
	if(status != OS_STATUS_OK)
    {
        logError("fails to sipTU_asGetCalledUser.");
       	rspCode = SIP_RESPONSE_500;
        status = OS_ERROR_INVALID_VALUE;
        goto BUILD_RESPONSE;
    }
	
	tuRegState_e calledRegState;
	pCalledContactUser = masReg_getUserRegInfo(&calledUri, &calledRegState);
	if(!pCalledContactUser || calledRegState != MAS_REGSTATE_REGISTERED)
	{
    	logInfo("called user (%r) is not registered or null pCalledContactUser, store the received SMS.", &calledUri);
		status = masDbStoreSms(-1, &calledUri, &callerUri, &sms);
        rspCode = SIP_RESPONSE_202;
        goto BUILD_RESPONSE;
	}	

#if 0
	//prepare for sipTU_buildRequest(), add a P-Called-ID 
	sipTuHdrRawValueStr_t calledId = {SIP_HDR_P_CALLED_PARTY_ID, {false, {&pCalledContactUser->sipUser}}};
#endif

	size_t topViaProtocolPos = 0;
	sipTransInfo_t sipTransInfo;
	osIpPort_t osIpPort;
	osConvertntoPL(pSipTUMsg->pPeer, &osIpPort);
	sipTransInfo.transId.viaId.host = osIpPort.ip.pl;
	sipTransInfo.transId.viaId.port = osIpPort.port;
	debug("PEER=%A", pSipTUMsg->pPeer); 

    osMBuf_t* pReq = masSip_buildRequest(&calledUri, &callerUri, pCalledContactUser, &sms, &sipTransInfo.transId.viaId, &topViaProtocolPos);

	masInfo_t* pMasInfo = osmalloc(sizeof(masInfo_t), masInfo_cleanup);
	pMasInfo->pSrcTransId = pSipTUMsg->pTransId;
	pMasInfo->smsType = MAS_SMS_TYPE_B2B;
	osDPL_dup(&pMasInfo->uacData.user, &calledUri);
	osDPL_dup(&pMasInfo->uacData.caller, &callerUri);
	osDPL_dup(&pMasInfo->uacData.sms, &sms); 
//to-do, shall not it be calledUri?
	pMasInfo->regId = masReg_addAppInfo(&callerUri, pMasInfo);
	if(!pMasInfo->regId)
	{
		osMBuf_dealloc(pReq);
		osfree(pMasInfo);
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
        goto BUILD_RESPONSE;
    }

	//notify tr the appId in case tr wants to close the transaction before sms app sends back response
	sipTU_mapTrTuId(pSipTUMsg->pTransId, pMasInfo);

    logInfo("SIP Request Message=\n%M", pReq);

	sipTransMsg_t sipTransMsg;
	sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
    sipTransMsg.isTpDirect = false;
    sipTransMsg.appType = SIPTU_APP_TYPE_MAS;

	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;

    sipTransInfo.isRequest = true;
	sipTransInfo.transId.reqCode = SIP_METHOD_MESSAGE;
	sipTransMsg.request.pTransInfo = &sipTransInfo;
	osIpPort_t osPeer = {{pCalledContactUser->hostport.host, false, false}, pCalledContactUser->hostport.portValue};
	osConvertPLton(&osPeer, true, &sipTransMsg.request.sipTrMsgBuf.tpInfo.peer);
	sipConfig_getHost1(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local);
	sipTransMsg.request.sipTrMsgBuf.tpInfo.protocolUpdatePos = topViaProtocolPos;
	sipTransMsg.pTransId = NULL;
	sipTransMsg.appType = SIPTU_APP_TYPE_MAS;
	sipTransMsg.pSenderId = pMasInfo;
					
    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

	//masSMS does not need to keep pReq.  If other laters need it, it is expected they will add ref to it
    osfree(pReq);

	//send 202 response to the caller.
	rspCode = SIP_RESPONSE_202;
	goto BUILD_RESPONSE;

BUILD_RESPONSE:
	logInfo("create the response for the request from caller, rspCode=%d", rspCode);

	//to send the response to the caller side
	if(rspCode != SIP_RESPONSE_INVALID)
	{
		osIpPort_t osPeer;
		osConvertntoPL(pSipTUMsg->pPeer, &osPeer);
		sipHostport_t peer = {osPeer.ip.pl, osPeer.port};
	    sipHdrDecoded_t viaHdr={};
    	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to decode the top via hdr in sipDecodeHdr.");
        	goto EXIT;
    	}

        osMBuf_t* pSipResp = NULL;
        sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
        int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
        pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
        status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
        status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
        status = sipTU_msgBuildEnd(pSipResp, false);

        if(pSipResp)
        {
            logInfo("Response Message=\n%M", pSipResp);

            sipTransMsg_t sipTransMsg = {};

            //fill the peer transport info
            sipHdrViaDecoded_t* pTopVia = ((sipHdrMultiVia_t*)(viaHdr.decodedHdr))->pVia;
            sipHostport_t peerHostPort;
            sipTransport_e peerTpProtocol;
            sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);
            sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
			osIpPort_t osPeer = {{peerHostPort.host, false, false}, peerHostPort.portValue};
			osConvertPLton(&osPeer, true, &sipTransMsg.response.sipTrMsgBuf.tpInfo.peer);
			sipConfig_getHost1(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local);
            sipTransMsg.response.sipTrMsgBuf.tpInfo.protocolUpdatePos = 0;

            //fill the other info
            sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
			sipTransMsg.isTpDirect = false;
            sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
            sipTransMsg.pTransId = pSipTUMsg->pTransId;
    		sipTransMsg.appType = SIPTU_APP_TYPE_MAS;
            sipTransMsg.response.rspCode = rspCode;
            sipTransMsg.pSenderId = NULL;

            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

			//masSMS does not need to keep pSipResp.  If other laters need it, it is expected they will add ref to it
			osfree(pSipResp);
		}
        else
        {
            logError("fails to sipTU_buildResponse.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        }

		osfree(viaHdr.decodedHdr);
		osfree(pReqDecodedRaw);
	}

EXIT:
//this test log here is dangerous, as calledContact may not be dcleared for some case, remove it as soon as the debug is done
//logError("to-remove ASAP, calledContact, host=%r, port=%d", &pCalledContactUser->hostport.host, pCalledContactUser->hostport.portValue);

	DEBUG_END
	return status;
}


static osStatus_e masSMS_onSipResponse(sipTUMsg_t* pSipTUMsg)
{
    osStatus_e status = OS_STATUS_OK;

	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto CLEAN_UP;
	}

	masInfo_t* pMasInfo = pSipTUMsg->pTUId;
	if(!pMasInfo)
	{
		logError("received rspCode(%d), but pMasInfo=NULL", pSipTUMsg->sipMsgBuf.rspCode);
		goto CLEAN_UP;
	}		

	if(pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_200)
	{
		debug("received rspCode(%d) for masInfo (%p), ignore.", pSipTUMsg->sipMsgBuf.rspCode, pMasInfo);
		goto EXIT;
	}
	else if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_200 && pSipTUMsg->sipMsgBuf.rspCode < SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p)", pSipTUMsg->sipMsgBuf.rspCode, pMasInfo);

		//delete the stored SMS if successfullyr eceived by the called
		if(pMasInfo->smsType == MAS_SMS_TYPE_DB)
		{
			status = masDbDeleteSms(pMasInfo->uacData.dbSmsId);
		}
		goto CLEAN_UP;
	}
	else if(pSipTUMsg->sipMsgBuf.rspCode >= SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p).", pSipTUMsg->sipMsgBuf.rspCode, pMasInfo);

		//for MAS_SMS_TYPE_DB, the msg has been stored and timer has been updated
		if(pMasInfo->smsType != MAS_SMS_TYPE_DB)
		{
            status = masDbStoreSms(-1, (osPointerLen_t*)&pMasInfo->uacData.user, (osPointerLen_t*)&pMasInfo->uacData.caller, (osPointerLen_t*)&pMasInfo->uacData.sms);
		}
	}

CLEAN_UP:
    masReg_deleteAppInfo(pMasInfo->regId, pMasInfo->pSrcTransId);
    osfree(pMasInfo);

EXIT:
	return status;
}


uint64_t masSMSStartTimer(time_t msec, void* pData)
{
    return osStartTimer(msec, masSMS_onTimeout, pData);
}


static osStatus_e masSMS_onSipTransError(sipTUMsg_t* pSipTUMsg)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
	if(!pSipTUMsg)
	{
		logError("null pointer, pSipTUMsg.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(!pSipTUMsg->pTUId)
	{
        logInfo("null pointer, pSipTUMsg->pTUId.");
        goto EXIT;
    }

	masReg_deleteAppInfo(((masInfo_t*)pSipTUMsg->pTUId)->regId, ((masInfo_t*)pSipTUMsg->pTUId)->pSrcTransId);

EXIT:
	DEBUG_END
	return status;
}
