#ifndef _SIP_TRANSACTION_H
#define _SIP_TRANSACTION_H


#include "osMBuf.h"

#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"

#include "sipTransIntf.h"
#include "transportIntf.h"


typedef enum {
	SIP_TRANS_STATE_NONE,	//this is not part of state machine, just indicate a transaction block is just created
	SIP_TRANS_STATE_TRYING,
	SIP_TRANS_STATE_CALLING,
	SIP_TRANS_STATE_PROCEEDING,
	SIP_TRANS_STATE_COMPLETED,
	SIP_TRANS_STATE_CONFIRMED,
	SIP_TRANS_STATE_TERMINATED,
} sipTransState_e;


typedef struct sipTransICTimer {
	uint64_t timerIdA;
	uint64_t timerIdB;
	uint64_t timerIdD;
} sipTransICTimer_t;


typedef struct sipTransNICTimer {
    uint64_t timerIdE;
    uint64_t timerIdF;
    uint64_t timerIdK;
} sipTransNICTimer_t;


typedef struct sipTransISTimer {
	uint64_t timerIdG;
    uint64_t timerIdH;
    uint64_t timerIdI;
} sipTransISTimer_t;


typedef struct sipTransNISTimer {
    uint64_t timerIdJ;
} sipTransNISTimer_t;

#if 0
typedef struct sipTransId {
    osPointerLen_t branchId;
    osPointerLen_t host;
    uint32_t port;
    sipRequest_e reqCode;
} sipTransId_t;
#endif


typedef osStatus_e (*sipTransSMOnMsg_h)(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);

typedef struct sipTransaction {
	sipTransState_e state;
	sipTuAppType_e appType;
//    bool isReq;
    sipMsgBuf_t req;	//contains the original request.  The branchId that is used to identify a transaction always points to this message via osPointerLen_t.  This implies this message shall not be changed and not be freed until the transaction is removed
    sipMsgBuf_t resp;	//will be updated each time a new response is received
	sipMsgBuf_t ack;
	sipTransId_t transId;	//content is based on pReq
	transportInfo_t tpInfo;	//ctrl;		
	union {
		sipTransICTimer_t sipTransICTimer;
		sipTransNICTimer_t sipTransNICTimer;
		sipTransISTimer_t sipTransISTimer;
		sipTransNISTimer_t sipTransNISTimer;
	};
	uint32_t timerAEGValue;		//the timer A/E/G timeout value
//	bool isUDP;
	osListElement_t* pTransHashLE;
	void* pTUId;
	sipTransSMOnMsg_h smOnMsg;
} sipTransaction_t;


uint64_t sipTransStartTimer(time_t msec, void* pData);


#endif
