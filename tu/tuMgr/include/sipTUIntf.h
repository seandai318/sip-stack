/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUIntf.h
 ********************************************************/

#ifndef _SIP_TU_INTF_H
#define _SIP_TU_INTF_H


#include <netinet/in.h>

#include "osList.h"
#include "osPL.h"
#include "osMBuf.h"
#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "transportIntf.h"



typedef enum {
    SIPTU_APP_TYPE_NONE,
    SIPTU_APP_TYPE_PROXY,
    SIPTU_APP_TYPE_MAS,
    SIPTU_APP_TYPE_REG,
	SIPTU_APP_TYPE_CSCF,
	SIPTU_APP_TYPE_ICSCF,
	SIPTU_APP_TYPE_SCSCF,
    SIPTU_APP_TYPE_COUNT,
} sipTuAppType_e;


typedef enum {
	SIP_TU_MSG_TYPE_MESSAGE,
	SIP_TU_MSG_TYPE_TRANSPORT_ERROR,	//due to local transport error
	SIP_TU_MSG_TYPE_RMT_NOT_ACCESSIBLE,	//when transport layer reports the remote is not accessible.  tpStatus  TCP_CONN_ERROR shall also map to this error
    SIP_TU_MSG_TYPE_TRANSACTION_ERROR,
} sipTUMsgType_e;


typedef struct {
	bool isServerTransaction;	//if failure happens in a client transaction/transport
} sipTuErrorInfo_t;


typedef struct sipTUMsg {
	sipTUMsgType_e sipTuMsgType;
    sipMsgType_e sipMsgType;	//to-do, move into sipMsgBuf_t and replace isRequest in sipMsgBuf_t with sipMsgType_e
	sipTuAppType_e appType;
	struct sockaddr_in* pPeer;
	struct sockaddr_in* pLocal;
	union {
    	sipMsgBuf_t  sipMsgBuf;			//contains raw sip buffer when sipTuMsgType = SIP_TU_MSG_TYPE_MESSAGE
		sipTuErrorInfo_t errorInfo;		//contain extra error info if the sipMsgType != SIP_TU_MSG_TYPE_MESSAGE 
	};
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
/*
 * branchExtraStr: a string that caller wants to be inserted into branch ID.
 * pParamList: list of sipHdrParamNameValue_t, like: sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
 * pParamList: a list of header parameters other than branchId.
 */
osStatus_e sipTU_addOwnVia(osMBuf_t* pMsgBuf, char* branchExtraStr, osList_t* pParamList, osPointerLen_t* pBranchId, osPointerLen_t* pHost, uint32_t* pPort, size_t* pProtocolViaPos);
void sipTUMsg_cleanup(void* data);


#endif
