#ifndef _SIP_TRANSPORT_H
#define _SIP_TRANSPORT_H

#include "osPL.h"
#include"osTypes.h"

#include "sipUriparam.h"


typedef struct sipTransportIpPort {
    osPointerLen_t ip;
    int port;
} sipTransportIpPort_t;


/*tpId is supposed to store the tcpFd related TCM address to speed up the search of tcm when needed.  But the use of tpId requires the sem for transport server TCM, since the caller is transaction, and there may have multiple transaction threads and they are different threads as transport server threads.  as such, we make the following decisions:
1. only when receiving a tcp packet then isUsed flag inside keepAlive structure will be updated in the TCM, when sending a tcp packet using a specified tcpFd, TCM would not be updated.  This effectively means that if a tcpFd keeps sending TCP packets, but never receive a packet, the keepAliveTimer will close this tcpFd.  I do not see this as a problem.
2. tcp sending is a direct function call by a sender.
*/
typedef struct sipTransportInfo {
	bool isServer;
    sipTransport_e tpType;
    int tcpFd;     			//if fd=-1, transport shall create one. for SIP response, the fd usually not 0, shall be the one what the request was received from
//	void* tpId;				//if tpId != NULL, it stores the relevant tcm address
    sipTransportIpPort_t peer;
    sipTransportIpPort_t local;
    size_t viaProtocolPos;  //if viaProtocolPos=0, do not update
} sipTransportInfo_t;


typedef struct sipTransportCtrl_t {
	void* pTrId;
	sipTransportInfo_t tpInfo;
} sipTransportCtrl_t;


#if 0
typedef struct sipTransportSetting {
	int epollId;	//if epollId = -1, thread has to create its own epollId
    int ownIPCfd;
    int timerfd;
    sipTransportIpPort_t local;
} sipTransportSetting_t;
#endif

#endif
