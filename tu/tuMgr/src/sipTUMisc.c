#include "osTypes.h"
#include "osDebug.h"

#include "sipConfig.h"
#include "sipTransIntf.h"
#include "sipTransport.h"
#include "sipHdrVia.h"


void* sipTU_sendReq2Tr(sipRequest_e nameCode, osMBuf_t* pReq, sipTransViaInfo_t* pViaId, sipTransportIpPort_t* nextHop, bool isTpDirect, sipTuAppType_e appType, size_t topViaProtocolPos, void* pTuInfo)
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
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.ip = nextHop->ip;
    sipTransMsg.request.sipTrMsgBuf.tpInfo.peer.port = nextHop->port;
    sipConfig_getHost(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.request.sipTrMsgBuf.tpInfo.local.port);
    sipTransMsg.request.sipTrMsgBuf.tpInfo.viaProtocolPos = topViaProtocolPos;
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
    sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.ip = peerHostPort.host;
    sipTransMsg.response.sipTrMsgBuf.tpInfo.peer.port = peerHostPort.portValue;
    sipConfig_getHost(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local.ip, &sipTransMsg.response.sipTrMsgBuf.tpInfo.local.port);
    sipTransMsg.response.sipTrMsgBuf.tpInfo.viaProtocolPos = 0;

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

