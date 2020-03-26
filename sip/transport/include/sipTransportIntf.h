#ifndef _SIP_TRANSPORT_INTF_H
#define _SIP_TRANSPORT_INTF_H

#include <netinet/in.h>

#include "osMBuf.h"

#include "sipTransport.h"


typedef enum {
	SIP_TRANSPORT_STATUS_UDP,
	SIP_TRANSPORT_STATUS_TCP_OK,
	SIP_TRANSPORT_STATUS_TCP_CONN,	//TCP CONN establish is ongoing
	SIP_TRANSPORT_STATUS_TCP_FAIL,
	SIP_TRANSPORT_STATUS_FAIL,
} sipTransportStatus_e;


//only used by transport client for TCP_READY and TCP_FAIL
typedef struct sipTransportStatusMsg {
	int tcpFd;
	void* pTransId;
} sipTransportStatusMsg_t;


typedef struct sipTransportMsgBuf {
    bool isServer;
    int tcpFd;      //if tcpFd=-1, the response may be sent via udp or another tcp connection
	struct sockaddr_in peer;
    osMBuf_t* pSipBuf;
//    void* tpId;     //contains the tcm address when tcpFd != 0
} sipTransportMsgBuf_t;


typedef void (*sipTp_appStartup_h)(void* pData);


//void sipTransport_onTimeout(uint64_t timerId, void* ptr);
sipTransportStatus_e sipTransport_send(void* pTrId, sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf);
void sipTransportMsgBuf_free(void* pData);

#endif
