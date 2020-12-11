/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTransIntf.h
 ********************************************************/

#ifndef _SIP_TRANS_INTF_H
#define _SIP_TRANS_INTF_H

#include "osPL.h"
#include "osTypes.h"

#include "sipMsgRequest.h"
#include "sipMsgFirstLine.h"
#include "sipTUIntf.h"
//#include "sipTransport.h"
#include "transportIntf.h"


typedef enum {
    SIP_TRANS_MSG_TYPE_TIMEOUT,     		//timeout
    SIP_TRANS_MSG_TYPE_PEER,        		//received msg from peer
    SIP_TRANS_MSG_TYPE_TU,          		//received msg from TU
	SIP_TRANS_MSG_TYPE_TX_TCP_READY,		//TCP connection is ready to send message
    SIP_TRANS_MSG_TYPE_TX_FAILED,   		//msg transmission failure
    SIP_TRANS_MSG_TYPE_INTERNAL_ERROR,  	//internal error, like memory can not be allocated, timer can not be started, etc.
	SIP_TRANS_MSG_TYPE_TU_FORCE_TERM_TRANS,	//forcefully terminate a transaction, like when timerIdC timesout, TU may request to terminate a transaction
} sipTransMsgType_e;


typedef enum {
	SIP_TRANS_MSG_CONTENT_REQUEST,		//corresponds to sipMsgType=SIP_MSG_REQUEST
	SIP_TRANS_MSG_CONTENT_RESPONSE,		//corresponds to sipMsgType=SIP_MSG_RESPONSE
	SIP_TRANS_MSG_CONTENT_ACK,			//corresponds to sipMsgType=SIP_MSG_ACK
//	SIP_TRANS_MSG_CONTENT_TCP_READY,	//corresponds to sipTransMsgType_e = SIP_TRANS_MSG_TYPE_TX_TCP_READY
} sipTransMsgContent_e;


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


typedef struct sipTransMsgBuf {
    sipMsgBuf_t sipMsgBuf;
	transportInfo_t tpInfo;
#if 0
	sipTransport_e tpType;
	uint64_t tcpFd;
    sipTransportIpPort_t peer;
    sipTransportIpPort_t local;
    size_t viaProtocolPos;  //if viaProtocolPos=0, do not update
#endif
}sipTransMsgBuf_t;


typedef struct sipTransMsgRequest {
	sipTransMsgBuf_t sipTrMsgBuf;
	sipTransInfo_t* pTransInfo;
} sipTransMsgRequest_t;


typedef struct sipTransMsgResponse {
    sipTransMsgBuf_t sipTrMsgBuf;
    sipResponse_e rspCode;
} sipTransMsgResponse_t;


typedef struct sipTransMsgAck {
    sipTransMsgBuf_t sipTrMsgBuf;
} sipTransMsgAck_t;

#if 0
typedef struct sipTransMsgTcpReady {
	int tcpFd;
} sipTransMsgTcpReady_t;
#endif

//only used when TU or transport sends a msg to transaction state machine.  For transaction state machine -> TU, directly send sipTransaction_t
typedef struct sipTransMsg {
    sipTransMsgContent_e sipMsgType;
	sipTuAppType_e appType;		//for TU to determine where to route a message, for response only
	bool isTpDirect;		//if=1, bypass transaction SM, otherwise, via transaction SM
//	sipTransMsgBuf_t sipTrMsgBuf;
//    sipMsgBuf_t* pSipMsg;
	union {
		sipTransMsgRequest_t request;		//used for SIP_TRANS_MSG_CONTENT_REQUEST
		sipTransMsgResponse_t response;		//used for SIP_TRANS_MSG_CONTENT_RESPONSE
		sipTransMsgAck_t ack;				//used for SIP_TRANS_MSG_CONTENT_ACK
//		sipTransMsgTcpReady_t tcpReady;		//used for SIP_TRANS_MSG_CONTENT_TCP_READY
//    	sipTransInfo_t* pTransInfo;	//used only when sipMsgType=SIP_MSG_REQUEST
//		sipResponse_e rspCode;		//used only when sipMsgType=SIP_MSG_RESPONSE
	};
    void* pTransId;		//store sipTransaction_t
	void* pSenderId;	//if the msg is from TU, it is TUId (regData or masData), if the msg is from transport, and transport does not have Id, this is NULL.
} sipTransMsg_t;


#if 0
typedef struct sipTransportMsgBuf {
	bool isServer;
    osMBuf_t* pSipBuf;
    int tcpFd;      //if tcpFd=-1, the response may be sent via udp or another tcp connection
//    void* tpId;     //contains the tcm address when tcpFd != 0
} sipTransportMsgBuf_t;
#endif

//typedef osStatus_e (*sipTransSMOnMsg_h)(sipTransMsgType_e msgType, void* pMsg, uint64_t timerId);

osStatus_e sipTransInit(uint32_t bucketSize);
osStatus_e sipTrans_onMsg(sipTransMsgType_e msgType, void* pData, uint64_t timerId);
//used for server app to set TuId when receiving a request from tr so that tr can notify it when there is something wrong before the server ever returns any message back to tr
void sipTr_setTuId(void* pTrId, void* pTuId);	

#endif
