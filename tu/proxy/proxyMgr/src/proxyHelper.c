#include "osTypes.h"
#include "osDebug.h"
#include "osMemory.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipHdrVia.h"
#include "sipTransIntf.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTUMisc.h"
#include "transportIntf.h"
#include "proxyHelper.h"


//proxy forwards the request
osStatus_e sipProxy_forwardReq(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipUri_t* pTargetUri,  bool isAddRR, transportIpPort_t* pNextHop, bool isTpDirect, proxyInfo_t* proxyInfo, void** ppTransId)

{
	osStatus_e status = OS_STATUS_OK;
	transportIpPort_t nextHop;
	osMBuf_t* pReq = NULL;

	if(!pSipTUMsg || !pReqDecodedRaw || !proxyInfo)
	{
		logError("null pointer, pSipTUMsg=%p, pReqDecodedRaw=%p, proxyInfo=%p", pSipTUMsg, pReqDecodedRaw, proxyInfo);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(!pSipTUMsg->pSipMsgBuf->isRequest)
	{
		logError("not a request.");
        status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//add the top via, remove the top route if exists, reduce max-forward by 1
    size_t topViaProtocolPos = 0;
    sipTransInfo_t sipTransInfo;
    sipTransViaInfo_t viaId;
#if 0	//use network address
    viaId.host = pSipTUMsg->pPeer->ip;
    viaId.port = pSipTUMsg->pPeer->port;
#else
	osIpPort_t osPeer;
	osConvertntoPL(pSipTUMsg->pPeer, &osPeer);
	viaId.host = osPeer.ip;
	viaId.port = osPeer.port;
#endif
//logError("to-remove, PEER, host=%r, port=%d", &pSipTUMsg->pPeer->ip, pSipTUMsg->pPeer->port);

    //prepare message forwarding
    sipHdrRawValueId_t delList = {SIP_HDR_ROUTE, true};
    uint8_t delNum = pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE] ? 1 : 0;

	uint8_t addNum = 0;
	char* ipPort = NULL;
	int len = 0;
    if(isAddRR)
	{
    	char* localIP;
    	int localPort;
    	sipConfig_getHostStr(&localIP, &localPort);
    	ipPort = oszalloc_r(SIP_HDR_MAX_SIZE, NULL);
    	len = sprintf(ipPort, "Record-Route: <sip:%s:%d;lr>\r\n", localIP, localPort);
		addNum = 1;
	}
	osPointerLen_t rr = {ipPort, len};
    sipHdrRawValueStr_t addList = {SIP_HDR_RECORD_ROUTE, rr};
	
    //forward the SIP INVITE, add top via, remove top Route, reduce the max-forarded by 1.  The viaId shall be filled with the real peer IP/port
    pReq = sipTU_b2bBuildRequest(pReqDecodedRaw, true, &delList, delNum, &addList, addNum, &viaId, pTargetUri, &topViaProtocolPos);
    osfree(ipPort);
    if(!pReq)
    {
        logError("fails to create proxy request.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    logInfo("SIP Request Message=\n%M", pReq);

	if(pNextHop == NULL)
	{
		//get the 2nd route hostname if exists, otherwise, get from req Uri
		status = sipProxy_getNextHopFrom2ndHdrValue(SIP_HDR_ROUTE, pReqDecodedRaw, &nextHop);
		if(status != OS_STATUS_OK)
		{
            goto EXIT;
        }

		if(nextHop.ip.l == 0)
		{
            sipFirstline_t firstLine;
            status = sipParser_firstLine(pSipTUMsg->pSipMsgBuf->pSipMsg, &firstLine, true);
            nextHop.ip = firstLine.u.sipReqLine.sipUri.hostport.host;
            nextHop.port = firstLine.u.sipReqLine.sipUri.hostport.portValue;
		}
	}

    void* pTransId = sipTU_sendReq2Tr(pSipTUMsg->pSipMsgBuf->reqCode, pReq, &viaId, pNextHop ? pNextHop : &nextHop, isTpDirect, SIPTU_APP_TYPE_PROXY, topViaProtocolPos, proxyInfo);
	if(!pTransId && !isTpDirect)
	{
		logError("fails to sipTU_sendReq2Tr.");
		status = OS_ERROR_SYSTEM_FAILURE;
	}

	if(ppTransId)
	{
		*ppTransId = pTransId;
	}

EXIT:
    //proxy does not need to keep pReq.  If other laters need it, it is expected they will add ref to it
    osfree(pReq);
	
	return status;
}


//proxy forwards the request
osStatus_e sipProxy_forwardResp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo)
{
    osStatus_e status = OS_STATUS_OK;
    transportIpPort_t nextHop;
    osMBuf_t* pResp = NULL;

    if(pSipTUMsg->pSipMsgBuf->isRequest)
    {
        logError("not a response.");

        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//remove the top via, copy the remaining
	sipHdrRawValueId_t hdrDelList = {SIP_HDR_VIA, true};
	pResp = sipTU_buildProxyResponse(pReqDecodedRaw, &hdrDelList, 1, NULL, 0);
	if(!pResp)
	{
		logError("fails to sipTU_buildProxyResponse.");
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	logInfo("the response message=\n%M", pResp);

	//the peerViaIdx=1, as pReqDecodedRaw is what has been received from peer UAS, the top via is proxy itself
	status = sipTU_sendRsp2Tr(pSipTUMsg->pSipMsgBuf->rspCode, pResp, pReqDecodedRaw, 1, pTransId, proxyInfo);

EXIT:
    //proxy does not need to keep pResp.  If other laters need it, it is expected they will add ref to it
	osfree(pResp);
	return status;
}


osStatus_e sipProxy_uasResponse(sipResponse_e rspCode, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo)
{
	osStatus_e status = OS_STATUS_OK;

    if(rspCode != SIP_RESPONSE_INVALID)
    {
        osMBuf_t* pSipResp = NULL;
        sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
        int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);
        pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);

		sipHdrDecoded_t viaHdr={};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);

#if 0	//use network address
        sipHostport_t peer;
        peer.host = pSipTUMsg->pPeer->ip;
        peer.portValue = pSipTUMsg->pPeer->port;
#else
		osIpPort_t osPeer;
		osConvertntoPL(pSipTUMsg->pPeer, &osPeer);
		sipHostport_t peer = {osPeer.ip, osPeer.port};
#endif
        status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
        osfree(viaHdr.decodedHdr);

        status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
        status = sipTU_msgBuildEnd(pSipResp, false);

		if(pSipResp)
		{
            logInfo("Response Message=\n%M", pSipResp);

			status = sipTU_sendRsp2Tr(rspCode, pSipResp, pReqDecodedRaw, 0, pSipTUMsg->pTransId, proxyInfo);
		}

        //proxy does not need to keep pSipResp.  If other laters need it, it is expected they will add ref to it
        osfree(pSipResp);
	}

EXIT:
	return status;
}


osStatus_e sipProxy_getNextHopFrom2ndHdrValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, transportIpPort_t* pNextHop)
{
	osStatus_e status = OS_STATUS_OK;

	sipHdrDecoded_t focusHdrDecoded;
	sipHdrGenericNameParamDecoded_t* p2ndHdrValue = NULL;

	status = sipHdrMultiNameParam_get2ndHdrValue(hdrCode, pReqDecodedRaw, &focusHdrDecoded, &p2ndHdrValue);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipHdrMultiNameParam_get2ndHdrValue, hdrCode=%d.", hdrCode);
		goto EXIT;
	}

	if(p2ndHdrValue)
	{	
    	pNextHop->ip = p2ndHdrValue->hdrValue.uri.hostport.host;
    	pNextHop->port = p2ndHdrValue->hdrValue.uri.hostport.portValue;

	    osfree(focusHdrDecoded.decodedHdr);
	}
	else
	{
		pNextHop->ip.l = 0;
	}

EXIT:
	return status;
}

