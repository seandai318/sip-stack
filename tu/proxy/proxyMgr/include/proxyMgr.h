/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyMgr.h
 ********************************************************/

#ifndef _PROXY_MGR
#define _PROXY_MGR

#include "osTypes.h"
#include "osList.h"
#include "osHash.h"
#include "osPL.h"

#include "sipMsgRequest.h"
#include "sipTUIntf.h"
#include "sipTU.h"
#include "sipTUMisc.h"
#include "proxyConfig.h"


#define PROXY_HASH_SIZE	4096


typedef enum {
	SIP_PROXY_STATUS_NONE,
	SIP_PROXY_STATUS_ESTABLISHED,
	SIP_PROXY_STATUS_DELETE,
} proxyStatus_e;


typedef struct proxyTranInfo {
    sipRequest_e method;
    uint32_t seqNum;
    void* pTransUas;
    void* pTransUac;
} proxyTranInfo_t;


struct proxyInfo;
struct sipProxyRouteModCtl;


typedef osStatus_e (*proxy_onMsgCallback) (sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, struct sipProxyRouteModCtl* pRouteCtl, struct proxyInfo** ppProxyInfo, void* pProxyMgrInfo);


typedef struct proxyInfo {
	proxy_onMsgCallback proxyOnMsg;	
	void* pCallInfo;	//contains proxy user info, like pRegInfo (scscfRegInfo_t) for scscf, etc. will be passed back as ((proxyInfo_t*)pSipTUMsg->pTUId)->pCallInfo when proxyOnMsg is called
	sipTuRR_t* pOwnRR;	//contains own RR, can be NULL if app does not need this info, proxyInfo is responsible to free this memory
} proxyInfo_t;


typedef struct {
    bool isAuto;    //if true, the function that this structure is passed in will add/remove route own its own, otherwise, purely based n passed in extraAddHdr/delHdr
    union {
        bool isAddRR;   //when isAuto = true;
        struct {        //when isAuto = false
            bool isChangeCallId;    //most a proxy would not change callid, but sometimes like when SCSCF forwards REGISTER to AS, callid is changed
            sipPointerLen_t newCallId;  //contains new callId when isChangeCallId = true, feed back to the caller
            sipTuHdrRawValueStr_t extraAddHdr[SIP_TU_PROXY_MAX_EXTRA_HDR_ADD_NUM];
            int addNum;
            sipHdrRawValueId_t extraDelHdr[SIP_TU_PROXY_MAX_EXTRA_HDR_DEL_NUM];
            int delNum;
        };
    };
} sipProxy_msgModInfo_t;


//this data structure is used to control the routing setting for a initial request when the request is re-directed via app
typedef struct sipProxyRouteModCtl {
	sipTuAddr_t* pNextHop;
	sipTuRR_t* pOwnRR;
	sipProxy_msgModInfo_t msgModInfo;
}sipProxyRouteModCtl_t;


//typedef proxyInfo_t* (*proxyCreationCB_h) (void* pProxyMgrInfo);
typedef void (*proxyStatusNtfyCB_h) (void* pProxyMgrInfo, proxyInfo_t* pProxy, proxyStatus_e proxyStatus);
typedef void* (*proxyReg2RegistrarCB_h) (osPointerLen_t* pSipUri, void* pMasInfo);
typedef osStatus_e (*proxyDelFromRegistrarCB_h) (void* pRegId, void* pTransId);

osStatus_e proxy_init(proxyStatusNtfyCB_h proxyStatusNtfy);
osStatus_e saProxy_init(uint32_t bucketSize, proxyReg2RegistrarCB_h proxyReg2Registrar, proxyDelFromRegistrarCB_h proxyDelFromRegistrar);
//osStatus_e proxyInit(proxyStatusNtfyCB_h proxyStatusNtfy, proxyReg2RegistrarCB_h proxyReg2Registrar, proxyDelFromRegistrarCB_h proxyDelFromRegistrar);
osStatus_e proxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
osStatus_e proxy_onSipTUMsgViaApp(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipProxyRouteModCtl_t* pRouteCtl, proxyInfo_t** ppProxy, void* pProxyMgrInfo);

osStatus_e sipProxy_addTrPair(osListPlus_t* pList, proxyTranInfo_t* pProxyTrInfo, bool isPrimary);
//if isPrimary=true, find the pairUasTrId from the pList->first parameter
osStatus_e sipProxy_updatePairUacTrId(osListPlus_t* pList, void* uasTrId, void* uacTrId, bool isPrimary);
void* sipProxy_getPairUasTrId(osListPlus_t* pList, void* uacTrId, bool isPrimary, bool isRemove); 
void* sipProxy_getPairPrimaryUasTrId(osListPlus_t* pList);
void sipProxy_getPairPrimaryTrId(osListPlus_t* pList, void** ppUasId, void** ppUacId);
osStatus_e sipProxy_removePairTrInfo(osListPlus_t* pList, void* trId, bool isPrimary);
osHash_t* proxy_getHash();

void proxyInfo_cleanup(void* pData);

#endif
