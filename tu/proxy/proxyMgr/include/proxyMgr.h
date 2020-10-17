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

#include "sipMsgRequest.h"
#include "sipTUIntf.h"



#define PROXY_HASH_SIZE	4096

typedef struct proxyTranInfo {
    sipRequest_e method;
    uint32_t seqNum;
    void* pTransUas;
    void* pTransUac;
} proxyTranInfo_t;



typedef osStatus_e (*proxy_onMsgCallback) (sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, osListElement_t* pHashLE);

typedef struct proxyInfo {
	proxy_onMsgCallback proxyOnMsg;	
	void* pCallInfo;		//contains proxy user info
} proxyInfo_t;



osStatus_e proxy_init(uint32_t bucketSize);
osStatus_e proxy_onSipTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
osStatus_e sipProxy_addTrPair(osListPlus_t* pList, proxyTranInfo_t* pProxyTrInfo, bool isPrimary);
//if isPrimary=true, find the pairUasTrId from the pList->first parameter
osStatus_e sipProxy_updatePairUacTrId(osListPlus_t* pList, void* uasTrId, void* uacTrId, bool isPrimary);
void* sipProxy_getPairUasTrId(osListPlus_t* pList, void* uacTrId, bool isPrimary, bool isRemove); 
void* sipProxy_getPairPrimaryUasTrId(osListPlus_t* pList);
void sipProxy_getPairPrimaryTrId(osListPlus_t* pList, void** ppUasId, void** ppUacId);
osStatus_e sipProxy_removePairTrInfo(osListPlus_t* pList, void* trId, bool isPrimary);
osHash_t* proxy_getHash();


#endif
