#ifndef _SIP_TU_INTF_H
#define _SIP_TU_INTF_H


#include <netinet/in.h>

#include "osList.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "transportIntf.h"
//#include "sipTransport.h" 


typedef enum {
    SIPTU_APP_TYPE_NONE,
    SIPTU_APP_TYPE_PROXY,
    SIPTU_APP_TYPE_MAS,
    SIPTU_APP_TYPE_REG,
    SIPTU_APP_TYPE_COUNT,
} sipTuAppType_e;


typedef enum {
	SIP_TU_MSG_TYPE_MESSAGE,
	SIP_TU_MSG_TYPE_NETWORK_ERROR,
    SIP_TU_MSG_TYPE_TRANSACTION_ERROR,
} sipTUMsgType_e;


typedef struct sipTUMsg {
    sipMsgType_e sipMsgType;
	sipTuAppType_e appType;
	struct sockaddr_in* pPeer;
//    transportIpPort_t* pPeer;
    sipMsgBuf_t* pSipMsgBuf;		//contains raw sip buffer
//	int tcpFd;						//tcp fd of the received message when > 0
	void* pTransId;
	void* pTUId;
#if 0
	osListElement_t* pTransHashId;	//transaction layer will pass this id to TU everytime sending a message to TU
    osListElement_t* pTUHashId;		//for a session, if transaction layer gets this Id from TU layer before, transaction layer will pass up to TU in the following messages.  This value is 0 if sipTrans does not have TU hashId value
#endif
} sipTUMsg_t;

typedef osStatus_e (*sipTUAppOnSipMsg_h)(sipTUMsgType_e msgType, sipTUMsg_t* pSipTUMsg);


//if sipTrans calls this function and get error response, it shall remove the transaction.  TU shall return OS_STATUS_OK even if it returns error response.  TU shall only return !OS_STATUS_OK if it can not habdle the SIP MESSAGE properly, like could not decode, memory error, etc. 
osStatus_e sipTU_onMsg(sipTUMsgType_e msgType, sipTUMsg_t* pMsg);
void sipTU_attach(sipTuAppType_e appType, sipTUAppOnSipMsg_h appOnSipMsg);

#endif
