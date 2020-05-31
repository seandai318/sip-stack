#ifndef _SIP_TCM_H
#define _SIP_TCM_H


#include <netinet/in.h>

#include "osTypes.h"
#include "osList.h"

#include "sipTransIntf.h"
//#include "sipTransport.h"
#include "transportIntf.h"



typedef enum {
    SIP_TRANSPORT_EOM_OTHER,    //other char
    SIP_TRANSPORT_EOM_R,        //"/r"
    SIP_TRANSPORT_EOM_RN,       //"/r/n"
    SIP_TRANSPORT_EOM_RNR,      //"/r/n/r"
    SIP_TRANSPORT_EOM_RNRN,     //"/r/n/r/n"
} sipTransportEomPattern_e;


typedef struct sipTpMsgState {
    sipTransportEomPattern_e eomPattern;
    int clValue;        //default shall be set to -1, means clValue has not bee set. clValue >= 0 means value has been found
    bool isBadMsg;      //bad msg format, or pSipMsgBuf is full, still not found a sip packet
} sipTpMsgState_t;

#if 0
typedef struct sipTpBuf {
    osMBuf_t* pSipMsgBuf;
    sipTpMsgState_t state;
} sipTpBuf_t;
#endif


#if 0
typedef struct {
    uint64_t keepAliveTimerId;
    bool isPersistent;  //if isPersistent, keepAliveTimerId shall not be lauched, the connection shall be kept as long as possible
    bool isUsed;    //if this TCP connection is ever used during the keepalive period
	sipTpMsgState_t state;
//    sipTpBuf_t sipBuf;
} sipTcpInfo_t;			//used to be tcpKeepAliveInfo_t;
#endif

#if 0
typedef struct sipTpQuarantine {
    bool isUsing;
    struct sockaddr_in peer;
    struct sockaddr_in local;
    uint64_t qTimerId;
} sipTpQuarantine_t;

#endif


struct tpTcm;

/*extract Content-Length and EOM from a newly received message piece
  pSipMsgBuf_pos = end of bytes processed.
  pSipMsgBuf->end = end of bytes received, except when a sip packet is found, which pSipMsgBuf->end = end of the sip packet
  if a sip packet does not contain Content-Length header, assume Content length = 0
  pNextStart: if there is extra bytes, the position in the buf that the extra bytes starts
  return value: -1: expect more read() for the current  sip packet, 0: exact sip packet, >1 extra bytes for next sip packet.
 */
ssize_t sipTpAnalyseMsg(osMBuf_t* pSipMsg, sipTpMsgState_t* pMsgState, size_t chunkLen, size_t* pNextStart);
//osStatus_e tpProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len);
osStatus_e tpProcessSipMsg(struct tpTcm* pTcm, int tcpFd, ssize_t len, bool* isForwardMsg);
//return value: true: discard msg, false, keep msg
bool sipTpSafeGuideMsg(osMBuf_t* sipBuf, size_t len);
void sipTpNotifyTcpConnUser(osListPlus_t* pList, transportStatus_e connStatus, int tcpfd, struct sockaddr_in* pPeer);



#endif
