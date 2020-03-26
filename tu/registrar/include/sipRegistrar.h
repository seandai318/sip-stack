#ifndef _SIP_REGISTRAR_H
#define _SIP_REGISTRAR_H


#include "osList.h"
#include "osPL.h"

#include "sipHeader.h"
#include "sipTUIntf.h"
//#include "masMgr.h"


#define SIP_REG_PURGE_TIMER		12		//sec.  testing value for now, need to make it bigger when official

typedef void (*sipReg_appActionOnRegister_h)(osPointerLen_t* user, void* pAppData);


typedef enum {
	TU_DEREG_CAUSE_TIMEOUT,
	TU_DEREG_CAUSE_USER_DEREG,
} tuDeRegCause_e;


typedef enum {
    MAS_REGSTATE_REGISTERED,
    MAS_REGSTATE_NOT_REGISTERED,	//the user exist in mAS, but is not registered
	MAS_REGSTATE_NOT_EXIST,			//the user does not exist in MAS
} tuRegState_e;


typedef struct tuRegistrar {
    tuRegState_e regState;
	bool isRspFailed;			//a temporary state to indicate if response sending was failed.  This is needed as masReg_onSipMsg, response tr handling, response tp handling are a chain of call, by the time sipTrans_onMsg() returns, many times the tr/tp handling state is already known, and tr has already notified masReg about the state.  tr set this value when error happened, masReg will use this value to do proper thing
	osDPointerLen_t user;		//it keeps its own memory, not point to other place like sipMsgBuf
//    sipMsgBuf_t* sipRegMsg;
    sipHdrDecoded_t* pContact;    //for now we only allow one contact per user
	osListElement_t* pRegHashLE;
    uint64_t expiryTimerId;
	uint64_t smsQueryTimerId;
	uint64_t purgeTimerId;
	osList_t appInfoList;
} tuRegistrar_t;


typedef struct sipRegAction {
    sipReg_appActionOnRegister_h appAction;
    void* pAppData;
} sipRegAction_t;


osStatus_e masReg_init(uint32_t bucketSize);
void sipReg_attach(osListApply_h applyHandler, sipRegAction_t* pRegActionData);
osStatus_e masReg_onTUMsg(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);
sipUri_t* masReg_getUserRegInfo(osPointerLen_t* pSipUri, tuRegState_e* pCalledRegState);
void* masReg_addAppInfo(osPointerLen_t* pSipUri, void* pMasInfo);
osStatus_e masReg_deleteAppInfo(void* pRegId, void* pTransId);
//isSrcTransId=true, the pTransId is a UAS, otherwise, pTransId ia a UAC
//void* masReg_getTransId(void* pTUId, void* pTransId, bool isSrcTransId);



#endif
