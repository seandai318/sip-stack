/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUMisc.h
 ********************************************************/

#ifndef _SIP_TU_MISC_H
#define _SIP_TU_MISC_H

#include "sipTU.h"
#include "sipTUIntf.h"


void* sipTU_sendReq2Tr(sipRequest_e nameCode, osMBuf_t* pReq, sipTransViaInfo_t* pViaId, sipTuAddr_t* nextHop,  bool isTpDirect, sipTuAppType_e appType, size_t topViaProtocolPos, void* pTuInfo);

osStatus_e sipTU_sendRsp2Tr(sipResponse_e rspCode, osMBuf_t* pResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, void* pTransId, void* pTuInfo);
osStatus_e sipTu_convertUri2NextHop(sipTuUri_t* pUri, transportIpPort_t* pNextHop);

#endif

