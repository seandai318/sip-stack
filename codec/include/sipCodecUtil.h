/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipCodecUtil.h
 ********************************************************/

#ifndef __SIP_CODEC_UTIL_H
#define __SIP_CODEC_UTIL_H


#include "sipMsgRequest.h"


#define sipDecodeTopRouteValue(pReqDecodedRaw, sipHdrDecoded, isDupRawHdr)	sipDecodeMGNPHdrTopValue(SIP_HDR_ROUTE, pReqDecodedRaw, sipHdrDecoded, isDupRawHdr)


void sipMsgBuf_copy(sipMsgBuf_t* dest, sipMsgBuf_t* src);
sipHdrGenericNameParam_t* sipDecodeMGNPHdrTopValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrDecoded_t* sipHdrDecoded, bool isDupRawHdr);
osStatus_e sipDecode_getMGNPHdrURIs(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pUser, int* userNum);


static inline bool sip_isRspCode2xx(uint32_t rspCode)
{
	return rspCode > 199 && rspCode < 300;
}


#endif
