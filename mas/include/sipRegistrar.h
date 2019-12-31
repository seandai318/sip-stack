#ifndef _MAS_REGISTRAR_H
#define _MAS_REGISTRAR_H


#include "osList.h"

#include "sipHeader.h"
#include "sipTUIntf.h"
#include "masMgr.h"


typedef enum {
	TU_DEREG_CAUSE_TIMEOUT,
	TU_DEREG_CAUSE_USER_DEREG,
} tuDeRegCause_e;


typedef enum {
    MAS_REGSTATE_REGISTERED,
    MAS_REGSTATE_NOT_REGISTERED,
} tuRegState_e;


typedef struct tuRegistrar {
    tuRegState_e regState;
	osPointerLen_t user;		//it keeps its own memory, not point to other place like sipMsgBuf
//    sipMsgBuf_t* sipRegMsg;
    sipHdrDecoded_t contact;    //for now we only allow one contact per user
	osListElement_t* pRegHashLE;
    uint64_t expiryTimerId;
	uint32_t purgeTimerId;
	osList_t appInfoList;
} tuRegistrar_t;


osStatus_e masReg_init(uint32_t bucketSize, osListApply_h applyHandler);
osStatus_e masReg_onSipMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
void* masReg_addAppInfo(osPointerLen_t* pSipUri, masInfo_t* pMasInfo);
osStatus_e masReg_deleteAppInfo(void* pTUId, void* pTransId);
//isSrcTransId=true, the pTransId is a UAS, otherwise, pTransId ia a UAC
void* masReg_getTransId(void* pTUId, void* pTransId, bool isSrcTransId);



#endif
