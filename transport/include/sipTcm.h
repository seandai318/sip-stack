#ifndef _SIP_TCM_H
#define _SIP_TCM_H


#include <netinet/in.h>

#include "osList.h"

#include "sipTransIntf.h"
#include "sipTransport.h"


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


typedef struct sipTpBuf {
    osMBuf_t* pSipMsgBuf;
    sipTpMsgState_t state;
} sipTpBuf_t;


typedef struct tcpKeepAliveInfo {
    uint64_t keepAliveTimerId;
    bool isPersistent;  //if isPersistent, keepAliveTimerId shall not be lauched, the connection shall be kept as long as possible
    bool isUsed;    //if this TCP connection is ever used during the keepalive period
    sipTpBuf_t sipBuf;
} tcpKeepAliveInfo_t;


typedef struct tcpConnInfo {
    osListPlus_t sipTrIdList;
} tcpConnInfo_t;


typedef struct sipTpTcm {
    bool isUsing;       //if this slot is current used
    bool isTcpConnDone; //if true, TCP connection has established
    int sockfd;         //tcp fd
    struct sockaddr_in peer;
    union {
        tcpConnInfo_t tcpConn;              //when isTcpConn = false
        tcpKeepAliveInfo_t tcpKeepAlive;    //when isTcpConn = true
    };
} sipTpTcm_t;


typedef struct sipTpQuarantine {
    bool isUsing;
    struct sockaddr_in peer;
    struct sockaddr_in local;
    uint64_t qTimerId;
} sipTpQuarantine_t;


typedef void (*notifyTcpConnUser_h)(osListPlus_t* pList, sipTransMsgType_e msgType, int tcpfd);


void sipTcmInit(notifyTcpConnUser_h notifier);
sipTpTcm_t* sipTpGetTcm(struct sockaddr_in peer, bool isReseveOne);
sipTpTcm_t* sipTpGetTcmByFd(int tcpFd, struct sockaddr_in peer);
osStatus_e sipTpTcmAddFd(int tcpfd, struct sockaddr_in* peer);
sipTpTcm_t* sipTpGetConnectedTcm(int tcpFd);
osStatus_e sipTpDeleteTcm(int tcpfd);
void sipTpDeleteAllTcm();
osStatus_e sipTpTcmAddUser(sipTpTcm_t* pTcm, void* pTrId);
osStatus_e sipTpTcmUpdateConn(int tcpfd, bool isConnEstablished);
bool sipTpIsInQList(struct sockaddr_in peer);
osMBuf_t* sipTpTcmBufInit(sipTpBuf_t* pTpBuf, bool isAllocSipBuf);
//only usable when LM_TRANSPORT module is set to DEBUG level
void sipTpListUsedTcm();

#endif
