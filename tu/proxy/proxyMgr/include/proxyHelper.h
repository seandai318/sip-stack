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



typedef struct {
    bool isAuto;    //if true, the function that this structure is passed in will add/remove route own its own, otherwise, purely based n passed in extraAddHdr/delHdr
    union {
        bool isAddRR;   //when isAuto = true;
        struct {        //when isAuto = false
			bool isChangeCallId;	//most a proxy would not change callid, but sometimes like when SCSCF forwards REGISTER to AS, callid is changed
			sipPointerLen_t newCallId;	//contains new callId when isChangeCallId = true, feed back to the caller
            sipHdrRawValueStr_t extraAddHdr[SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM];
            int addNum;
            sipHdrRawValueId_t extraDelHdr[SIP_TU_PROXY_MAX_EXTRA_HDR_DEL_NUM];
            int delNum;
        };
	};
} sipProxy_msgModInfo_t;


//pAppId has type void*, instead of proxyInfo_t, so that modules other than proxy can also use this function. For proxy, the pAppId is proxyInfo_t, it maybe other type for other app. 
osStatus_e sipProxy_forwardReq(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw,  sipTuUri_t* pTargetUri, sipProxy_msgModInfo_t* pHdrModInfo, sipTuAddr_t* pNextHop, bool isTpDirect, void* pAppId, void** ppTransId);

//pAppId has type void*, instead of proxyInfo_t, so that modules other than proxy can also use this function. For proxy, the pAppId is proxyInfo_t, it maybe other type for other app.
osStatus_e sipProxy_forwardResp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* pAppId);

osStatus_e sipProxy_getNextHopFrom2ndHdrValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, transportIpPort_t* pNextHop);
osStatus_e sipProxy_uasResponse(sipResponse_e rspCode, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo);


static inline void sipProxyMsgModInfo_addHdr(sipHdrRawValueStr_t* extraAddHdr, int* addNum, sipHdrName_e hdrCode, osPointerLen_t* pValue)
{
	if(!extraAddHdr || !pValue || !addNum || *addNum >= SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM)
	{
		return;
	}

	extraAddHdr[*addNum].nameCode = hdrCode;
	extraAddHdr[*addNum++].value = *pValue;
}


static inline void sipProxyMsgModInfo_delHdr(sipHdrRawValueId_t* extraDelHdr, int* delNum, sipHdrName_e hdrCode, bool isTopOnly)
{
	if(!extraDelHdr || !delNum || *delNum >= SIP_TU_PROXY_MAX_EXTRA_HDR_DEL_NUM)
	{
		return;
	}

	extraDelHdr[*delNum].nameCode = hdrCode;
	extraDelHdr[*delNum++].isTopOnly = isTopOnly;
}


#endif
