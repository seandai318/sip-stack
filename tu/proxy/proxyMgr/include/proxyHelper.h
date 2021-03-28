/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyHelper.h
 ********************************************************/

#ifndef _PROXY_HELPER_H
#define _PROXY_HELPER_H

#include "osTypes.h"

#include "proxyConfig.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipUri.h"
#include "sipTUIntf.h"
#include "transportIntf.h"
#include "proxyMgr.h"
#include "sipTU.h"


//pAppId has type void*, instead of proxyInfo_t, so that modules other than proxy can also use this function. For proxy, the pAppId is proxyInfo_t, it maybe other type for other app. 
osStatus_e sipProxy_forwardReq(sipTuAppType_e proxyType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw,  sipTuUri_t* pTargetUri, sipProxy_msgModInfo_t* pHdrModInfo, sipTuAddr_t* pNextHop, bool isTpDirect, void* pAppId, void** ppTransId);

//pAppId has type void*, instead of proxyInfo_t, so that modules other than proxy can also use this function. For proxy, the pAppId is proxyInfo_t, it maybe other type for other app.
osStatus_e sipProxy_forwardResp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* pAppId);

osStatus_e sipProxy_getNextHopFrom2ndHdrValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, transportIpPort_t* pNextHop);
osStatus_e sipProxy_uasResponse(sipResponse_e rspCode, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo);


static inline void sipProxyMsgModInfo_addHdr(sipTuHdrRawValueStr_t* extraAddHdr, int* addNum, sipHdrName_e hdrCode, sipTuHdrRawValue_t* pValue)
{
	if(!extraAddHdr || !pValue || !addNum || *addNum >= SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM)
	{
		return;
	}

	extraAddHdr[*addNum].nameCode = hdrCode;
	if(pValue->rawValueType == SIPTU_RAW_VALUE_TYPE_STR_SIPPL)
	{
		extraAddHdr[*addNum].value.rawValueType = pValue->rawValueType;
		sipPL_copy(&extraAddHdr[*addNum].value.sipPLValue, &pValue->sipPLValue);
	}
	else
	{
		extraAddHdr[*addNum].value = *pValue;
	}

	(*addNum)++;
}


static inline osPointerLen_t* sipProxyMsgModInfo_addSipPLHdr(sipTuHdrRawValueStr_t* extraAddHdr, int* addNum, sipHdrName_e hdrCode)
{
	if(!extraAddHdr || !addNum || *addNum >= SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM)
    {
        return NULL;
    }

	osPointerLen_t* pl = NULL;
	extraAddHdr[*addNum].nameCode = hdrCode;
	extraAddHdr[*addNum].value.rawValueType = SIPTU_RAW_VALUE_TYPE_STR_SIPPL;
	sipPL_init(&extraAddHdr[*addNum].value.sipPLValue);
	pl = &extraAddHdr[*addNum].value.sipPLValue.pl;
	(*addNum)++;

	return pl;
}


static inline void sipProxyMsgModInfo_delHdr(sipHdrRawValueId_t* extraDelHdr, int* delNum, sipHdrName_e hdrCode, bool isTopOnly)
{
	if(!extraDelHdr || !delNum || *delNum >= SIP_TU_PROXY_MAX_EXTRA_HDR_DEL_NUM)
	{
		return;
	}

	extraDelHdr[*delNum].nameCode = hdrCode;
	extraDelHdr[(*delNum)++].isTopOnly = isTopOnly;
}


#endif
