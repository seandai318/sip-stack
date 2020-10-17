/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyHelper.h
 ********************************************************/

#ifndef _PROXY_HELPER_H
#define _PROXY_HELPER_H

#include "osTypes.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipUri.h"
#include "sipTUIntf.h"
#include "transportIntf.h"
#include "proxyMgr.h"



osStatus_e sipProxy_forwardReq(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw,  sipUri_t* pTargetUri, bool isAddRR, transportIpPort_t* pNextHop, bool isTpDirect, proxyInfo_t* proxyInfo, void** ppTransId);

osStatus_e sipProxy_forwardResp(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo);

osStatus_e sipProxy_getNextHopFrom2ndHdrValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, transportIpPort_t* pNextHop);
osStatus_e sipProxy_uasResponse(sipResponse_e rspCode, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pTransId, void* proxyInfo);


#endif
