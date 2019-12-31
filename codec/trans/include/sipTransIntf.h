#ifndef _SIP_TRANS_INTF_H
#define _SIP_TRANS_INTF_H

#include "osPL.h"
#include "osTypes.h"

#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"


typedef enum {
    SIP_TRANS_MSG_TYPE_TIMEOUT,     //timeout
    SIP_TRANS_MSG_TYPE_PEER,        //received msg from peer
    SIP_TRANS_MSG_TYPE_TU,          //received msg from TU
    SIP_TRANS_MSG_TYPE_TX_FAILED,   //msg transmission failure
    SIP_TRANS_MSG_TYPE_INTERNAL_ERROR,  //internal error, like memory can not be allocated, timer can not be started, etc.
} sipTransMsgType_e;



typedef struct sipTransViaInfo {
    osPointerLen_t branchId;
    osPointerLen_t host;
    uint32_t port;
} sipTransViaInfo_t;


typedef struct sipTransId {
	sipTransViaInfo_t viaId;
    sipRequest_e reqCode;
} sipTransId_t;


//per message trans info
typedef struct sipTransInfo {
	bool isRequest;
	sipTransId_t transId;
	sipResponse_e rspCode;	//only relevant if the message is a response. TO-DO since sipTransMsg_t already has rspCode, consider remove this one 
} sipTransInfo_t;


//only used when TU or transport sends a msg to transaction state machine.  For transaction state machine -> TU, directly send sipTransaction_t
typedef struct sipTransMsg {
    sipMsgType_e sipMsgType;
	sipMsgBuf_t sipMsgBuf;
//    sipMsgBuf_t* pSipMsg;
	union {
    	sipTransInfo_t* pTransInfo;	//used only when sipMsgType=SIP_MSG_REQUEST
		sipResponse_e rspCode;		//used only when sipMsgType=SIP_MSG_RESPONSE
	};
    void* pTransId;		//store sipTransaction_t
	void* pSenderId;	//if the msg is from TU, it is TUId, if the msg is from transport, and transport does not have Id, this is NULL.
} sipTransMsg_t;


//typedef osStatus_e (*sipTransSMOnMsg_h)(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);

osStatus_e sipTransInit(uint32_t bucketSize);
osStatus_e sipTrans_onMsg(sipTransMsgType_e msgType, void* pData, uint64_t timerId);
	

#endif
