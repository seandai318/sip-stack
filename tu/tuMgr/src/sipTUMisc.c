/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUMisc.c
 ********************************************************/


#include "osTypes.h"
#include "osDebug.h"
#include "osSockAddr.h"

#include "sipConfig.h"
#include "sipTransIntf.h"
#include "transportIntf.h"
#include "sipHdrVia.h"


void* sipTU_sendReq2Tr(sipRequest_e nameCode, osMBuf_t* pReq, sipTransViaInfo_t* pViaId, transportIpPort_t* nextHop, bool isTpDirect, sipTuAppType_e appType, size_t topViaProtocolPos, void* pTuInfo)
{
	osStatus_e status = OS_STATUS_OK;
    sipTransInfo_t sipTransInfo;
	void* pTransId = NULL;

	sipTransInfo.isRequest = true;
	sipTransInfo.transId.viaId = *pViaId;
    sipTransInfo.transId.reqCode = nameCode;

    sipTransMsg_t sipTransMsg;
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
	sipTransMsg.isTpDirect = isTpDirect;
	sipTransMsg.appType = appType;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = nameCode;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;
    sipTransMsg.request.pTransInfo = &sipTransInfo;

	osIpPort_t ipPort = {{nextHop->ip, false, false}, nextHop->port};
	osConvertPLton(&ipPort, true, &sipTransMsg.request.sipTrMsgBuf.tpInfo.peer);

	sipConfig_getHost1(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.request.sipTrMsgBuf.tpInfo.protocolUpdatePos = topViaProtocolPos;
    sipTransMsg.pTransId = NULL;
    sipTransMsg.pSenderId = pTuInfo;

    if(sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0) == OS_STATUS_OK)
	{
		pTransId = sipTransMsg.pTransId;
	}
	
	return pTransId;
}


//peerViaIdx indicates from which via to get the peer address, if peerViaIdx=SIP_VIA_IDX_MAX, the pointed via is the bottom via
osStatus_e sipTU_sendRsp2Tr(sipResponse_e rspCode, osMBuf_t* pResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, void* pTransId, void* pAppId)
{
	osStatus_e status = OS_STATUS_OK;
    sipTransMsg_t sipTransMsg = {};

    //fill the peer transport info
    sipHostport_t peerHostPort;
    sipTransport_e peerTpProtocol;
  	sipHdrVia_getPeerTransportFromRaw(pReqDecodedRaw, peerViaIdx, &peerHostPort, &peerTpProtocol);

    sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
	osIpPort_t ipPort = {{peerHostPort.host, false, false}, peerHostPort.portValue};
	osConvertPLton(&ipPort, true, &sipTransMsg.response.sipTrMsgBuf.tpInfo.peer);
	sipConfig_getHost1(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.response.sipTrMsgBuf.tpInfo.protocolUpdatePos = 0;

    //fill the other info
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
	if(!pTransId)
	{
		sipTransMsg.isTpDirect = true;
	}
    sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pResp;
    sipTransMsg.pTransId = pTransId;
    sipTransMsg.response.rspCode = rspCode;
    sipTransMsg.pSenderId = pAppId;

    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

EXIT:
	return status;
}


