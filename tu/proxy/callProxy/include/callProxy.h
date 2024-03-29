/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file callProxy.h
 ********************************************************/

#ifndef _CALL_PROXY_H
#define _CALL_PROXY_H

#include "osList.h"
#include "osPL.h"
#include "osTypes.h"

#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipTUIntf.h"
#include "proxyMgr.h"
#include "sipTU.h"


typedef enum {
	SIP_CALLPROXY_STATE_NONE,
	SIP_CALLPROXY_STATE_INIT_INVITE,
    SIP_CALLPROXY_STATE_INIT_ERROR,
	SIP_CALLPROXY_STATE_INIT_200_RECEIVED,
	SIP_CALLPROXY_STATE_INIT_ACK_RECEIVED,
    SIP_CALLPROXY_STATE_BYE,
	SIP_CALLPROXY_STATE_SUB_INVITE,
    SIP_CALLPROXY_STATE_SUB_200_RECEIVED,
    SIP_CALLPROXY_STATE_SUB_ACK_RECEIVED,
} sipCallProxyState_e;



typedef struct callProxyInfo {
	sipCallProxyState_e state;
	sipTuAppType_e tuAppType;	//identify app that uses this proxy.  For saProxy, this value shall be set to SIPTU_APP_TYPE_NONE
	void* regId;
	proxyInfo_t* pProxyInfo;	//the object of proxy
	sipProxyAppInfo_t appInfo;	//contains the object of proxyMgr, like CSCF, and rspCallback
//	osListElement_t* pCallHashLE;
	uint32_t seqNum;
	osDPointerLen_t callId;
	sipTuAddr_t cancelNextHop;		//make sure the ip is cleaned up when this data structure is reclaimed
	uint64_t timerIdC;
	uint64_t timerIdWaitAck;
	osListPlus_t proxyTransInfo;
} callProxyInfo_t;


void callProxy_init(proxyStatusNtfyCB_h proxyStatusNtfy, proxyReg2RegistrarCB_h proxyReg2Registrar, proxyDelFromRegistrarCB_h proxyDelFromRegistrar);
osStatus_e callProxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteCtl, proxyInfo_t** ppProxyInfo, sipProxyAppInfo_t* pAppInfo);


#endif
