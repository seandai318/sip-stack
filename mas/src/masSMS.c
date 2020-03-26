#include "osHash.h"
#include "osTimer.h"

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

    sipMsgDecodedRawHdr_t* pReqDecodedRaw = sipDecodeMsgRawHdr(pSipTUMsg->pSipMsgBuf, NULL, 0);
    if(pReqDecodedRaw == NULL)
    {
        logError("fails to sipDecodeMsgRawHdr.  Since the received SIP message was not decoded, there will not be any sip response.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(pSipTUMsg->pSipMsgBuf->reqCode != SIP_METHOD_MESSAGE)
	{
   		logError("receives unexpected sip message type (%d).", pSipTUMsg->pSipMsgBuf->reqCode);
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
logError("to-remove, SMS, sms=%r", &sms);

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

logError("to-remove, calledContact, host=%r, port=%d", &pCalledContactUser->hostport.host, pCalledContactUser->hostport.portValue);

	//prepare for sipTU_buildRequest(), add a P-Called-ID 
	sipHdrRawValueStr_t calledId;
	calledId.nameCode = SIP_HDR_P_CALLED_PARTY_ID;
	calledId.value = pCalledContactUser->sipUser;

	size_t topViaProtocolPos = 0;
	sipTransInfo_t sipTransInfo;
	sipTransInfo.transId.viaId.host = pSipTUMsg->pPeer->ip;
	sipTransInfo.transId.viaId.port = pSipTUMsg->pPeer->port;
logError("to-remove, PEER, host=%r, port=%d", &pSipTUMsg->pPeer->ip, pSipTUMsg->pPeer->port); 

#if 0
    //forward the SIP MESSAGE back to CSCF, add top via, remove top Route, reduce the max-forarded by 1.  The viaId shall be filled with the real peer IP/port
	osMBuf_t* pReq = sipTU_b2bBuildRequest(pReqDecodedRaw, false, NULL, 0, &calledId, 0, &sipTransInfo.transId.viaId, pCalledContactUser, &topViaProtocolPos);
	if(!pReq)
	{
		logError("fails to create proxy request.");
		rspCode = SIP_RESPONSE_503;
        status = OS_ERROR_INVALID_VALUE;
        goto BUILD_RESPONSE;
	}
#endif
    osMBuf_t* pReq = masSip_buildRequest(&calledUri, &callerUri, pCalledContactUser, &sms, &sipTransInfo.transId.viaId, &topViaProtocolPos);

	//sipTransInfo.transId.viaId shall contains the request top via info, it has been updated in the function sipTU_buildRequest()
//	sipTransInfo.transId.viaId.host = pCalledContactUser.hostport.host;
//	sipTransInfo.transId.viaId.port = pCalledContactUser.hostport.portValue;

	masInfo_t* pMasInfo = osMem_alloc(sizeof(masInfo_t), masInfo_cleanup);
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
		rspCode = SIP_RESPONSE_500;
		status = OS_ERROR_INVALID_VALUE;
        goto BUILD_RESPONSE;
    }

    logInfo("SIP Request Message=\n%M", pReq);

	sipTransMsg_t sipTransMsg;
	sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
    sipTransInfo.isRequest = true;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = SIP_METHOD_MESSAGE;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
	sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;
	sipTransInfo.transId.reqCode = SIP_METHOD_MESSAGE;
	sipTransMsg.request.pTransInfo = &sipTransInfo;
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.ip = pCalledContactUser->hostport.host;
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.port = pCalledContactUser->hostport.portValue;
    sipConfig_getHost(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.request.sipTrMsgBuf.tpInfo.local.port);
	sipTransMsg.request.sipTrMsgBuf.tpInfo.viaProtocolPos = topViaProtocolPos;
	sipTransMsg.pTransId = NULL;
	sipTransMsg.pSenderId = pMasInfo;
					
    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

	//masSMS does not need to keep pReq.  If other laters need it, it is expected they will add ref to it
    osMem_deref(pReq);

	//send 202 response to the caller.
	rspCode = SIP_RESPONSE_202;
	goto BUILD_RESPONSE;

BUILD_RESPONSE:
	logInfo("create the response for the request from caller, rspCode=%d", rspCode);

	//to send the response to the caller side
	if(rspCode != SIP_RESPONSE_INVALID)
	{
        sipHostport_t peer;
        peer.host = pSipTUMsg->pPeer->ip;
        peer.portValue = pSipTUMsg->pPeer->port;

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
//            sipTransMsg.response.sipTrMsgBuf.tpInfo.tcpFd = pSipTUMsg->tcpFd;
            sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.ip = peerHostPort.host;
            sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.port = peerHostPort.portValue;
            sipConfig_getHost(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.response.sipTrMsgBuf.tpInfo.local.port);
            sipTransMsg.response.sipTrMsgBuf.tpInfo.viaProtocolPos = 0;

            //fill the other info
            sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
            sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
            sipTransMsg.pTransId = pSipTUMsg->pTransId;
            sipTransMsg.response.rspCode = rspCode;
            sipTransMsg.pSenderId = pMasInfo;

            status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

			//masSMS does not need to keep pSipResp.  If other laters need it, it is expected they will add ref to it
			osMem_deref(pSipResp);
		}
        else
        {
            logError("fails to sipTU_buildResponse.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        }

		osMem_deref(viaHdr.decodedHdr);
		osMem_deref(pReqDecodedRaw);
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
		logError("received rspCode(%d), but pMasInfo=NULL", pSipTUMsg->pSipMsgBuf->rspCode);
		goto CLEAN_UP;
	}		

	if(pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_200)
	{
		debug("received rspCode(%d) for masInfo (%p), ignore.", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);
		goto EXIT;
	}
	else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_200 && pSipTUMsg->pSipMsgBuf->rspCode < SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p)", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);

		//delete the stored SMS if successfullyr eceived by the called
		if(pMasInfo->smsType == MAS_SMS_TYPE_DB)
		{
			status = masDbDeleteSms(pMasInfo->uacData.dbSmsId);
		}
		goto CLEAN_UP;
	}
	else if(pSipTUMsg->pSipMsgBuf->rspCode >= SIP_RESPONSE_300)
	{
		debug("received rspCode(%d) for masInfo (%p).", pSipTUMsg->pSipMsgBuf->rspCode, pMasInfo);

		//for MAS_SMS_TYPE_DB, the msg has been stored and timer has been updated
		if(pMasInfo->smsType != MAS_SMS_TYPE_DB)
		{
            status = masDbStoreSms(-1, (osPointerLen_t*)&pMasInfo->uacData.user, (osPointerLen_t*)&pMasInfo->uacData.caller, (osPointerLen_t*)&pMasInfo->uacData.sms);
		}
	}

CLEAN_UP:
    masReg_deleteAppInfo(pMasInfo, pMasInfo->pSrcTransId);
    osMem_deref(pMasInfo);

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

	masReg_deleteAppInfo(pSipTUMsg->pTUId, ((masInfo_t*)pSipTUMsg->pTUId)->pSrcTransId);

EXIT:
	DEBUG_END
	return status;
}
