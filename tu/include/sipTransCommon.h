#ifndef _SIP_TRANS_COMMON_H
#define _SIP_TRANS_COMMON_H

#include "osPL.h"
#include "osTypes.h"

#include "sipMsgFirstLine.h"


typedef enum {
    SIP_TRANS_MSG_TYPE_TIMEOUT,     //timeout
    SIP_TRANS_MSG_TYPE_PEER,        //received msg from peer
    SIP_TRANS_MSG_TYPE_TU,          //received msg from TU
    SIP_TRANS_MSG_TYPE_TX_FAILED,   //msg transmission failure
    SIP_TRANS_MSG_TYPE_INTERNAL_ERROR,  //internal error, like memory can not be allocated, timer can not be started, etc.
} sipTransMsgType_e;


typedef struct sipTransId {
    osPointerLen_t branchId;
    osPointerLen_t host;
    uint32_t port;
    sipRequest_e reqCode;
} sipTransId_t;


//per message trans info
typedef struct sipTransInfo {
	bool isRequest;
	sipTransId_t transId;
	sipResponse_e rspCode;	//only relevant if the message is a response
} sipTransInfo_t;
	

#endif
