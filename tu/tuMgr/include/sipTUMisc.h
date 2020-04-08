#ifndef _SIP_TU_MISC_H
#define _SIP_TU_MISC_H

#include "sipTUIntf.h"


void* sipTU_sendReq2Tr(sipRequest_e nameCode, osMBuf_t* pReq, sipTransViaInfo_t* pViaId, sipTransportIpPort_t* nextHop,  bool isTpDirect, sipTuAppType_e appType, size_t topViaProtocolPos, void* pTuInfo);

osStatus_e sipTU_sendRsp2Tr(sipResponse_e rspCode, osMBuf_t* pResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, void* pTransId, void* pTuInfo);

#endif

