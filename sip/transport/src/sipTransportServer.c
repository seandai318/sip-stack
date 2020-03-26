#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "osDebug.h"
#include "osHash.h"
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"
#include "osResourceMgmt.h"

#include "sipConfig.h"
#include "sipMsgFirstLine.h"
#include "sipUriparam.h"
#include "sipTransMgr.h"
#include "sipTransportIntf.h"
#include "sipTransportServer.h"
#include "sipTcm.h"
#include "sipTransportLib.h"



static void sipTpServerOnIpcMsg(osIPCMsg_t* pIpcMsg);
static void sipTpServerTimeout(uint64_t timerId, void* ptr);
static void sipTpServerForwardMsg(osMBuf_t* pSipBuf, int tcpFd, struct sockaddr_in* peer);
static void sipTpServerUpdateLBInfo(void* pMsg);

static __thread sipTransportServerSetting_t tpSetting;
static __thread int tcpListenFd=-1, tpEpFd=-1;
static __thread osHash_t* lbHash;
static __thread int lbFd[SIP_CONFIG_TRANSACTION_THREAD_NUM];
static __thread osHash_t* sipTransportLBHash;
//can not be __thread since it may be used by transaction layer directly for sending message via UDP
static int udpFd=-1;

osStatus_e sipTransportServerInit(int pipefd[2], uint32_t bucketSize)
{
    osStatus_e status = OS_STATUS_OK;

    //create IPC pipes
    if(pipe2(pipefd, O_NONBLOCK | O_DIRECT) != 0)
    {
        logError("fails to pipe2, errno=%d.", errno);
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    sipTransportLBHash = osHash_create(bucketSize);
    if(!sipTransportLBHash)
    {
        logError("fails to create sipTransportLBHash.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


void* sipTransportServerStart(void* pData)
{
	struct sockaddr_in localAddr;
	struct epoll_event event, events[SYS_MAX_EPOLL_WAIT_EVENTS];

    sipTransportServerSetting_t tpSetting = *(sipTransportServerSetting_t*)pData;

    osTimerInit(tpSetting.timerfd, tpSetting.ownIpcFd[1], SIP_CONFIG_TIMEOUT_MULTIPLE, sipTpServerTimeout);

	for(int i=0; i<SIP_CONFIG_TRANSACTION_THREAD_NUM; i++)
	{
		lbFd[i] = tpSetting.lbFd[i];
	}

    debug("threadId = %u.", (unsigned int)pthread_self());

    tpEpFd = epoll_create1(0);
    if(tpEpFd == -1)
    {
       	logError("fails to create epoll file descriptor, errno=%d.\n", errno);
		goto EXIT;
	}

    event.events = EPOLLIN;
   	event.data.fd = tpSetting.ownIpcFd[0];
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, tpSetting.ownIpcFd[0], &event))
    {
        logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", tpSetting.ownIpcFd[0], tpEpFd, errno);
		goto EXIT;
	}

    memset(&localAddr, 0, sizeof(localAddr));
//    memset(&peerAddr, 0, sizeof(peerAddr));

	if(sipTpConvertPLton(&tpSetting.local, true, &localAddr) != OS_STATUS_OK)
	{
		logError("fails to sipTpConvertPLton for udp, IP=%r, port=%d.", &tpSetting.local.ip, tpSetting.local.port);
		goto EXIT;
	}

	//create UDP listening socket
    if((udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0 ) 
	{ 
		logError("fails to create UDP socket.");
		goto EXIT;
    } 

	int opt = 1;
	//has to set SO_REUSEADDR, otherwise, bind() will get EADDRINUSE(98) error when port is specified
	if(setsockopt(udpFd,SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) != 0)
	{
        logError("fails to setsockopt for SO_REUSEADDR.");
        goto EXIT;
    }

	if(setsockopt(udpFd,SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) != 0) 
	{
        logError("fails to setsockopt for SO_REUSEPORT.");
        goto EXIT;
    }
   
    // Bind the socket with the server address 
    if(bind(udpFd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) 
    { 
        logError("udpSocket bind fails, localIP=%r, udpSocketfd=%d, errno=%d.", &tpSetting.local.ip, udpFd, errno); 
		goto EXIT;
    } 

    event.events = EPOLLIN;
    event.data.fd = udpFd;
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, udpFd, &event))
    {
        logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", udpFd, tpEpFd, errno);
        goto EXIT;
    }

	debug("udp listening fd =%d (ip=%r, port=%d) is added into epoll fd (%d).", udpFd, &tpSetting.local.ip, tpSetting.local.port, tpEpFd);

	//create TCP listening socket
    if((tcpListenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        logError("fails to create TCP socket.");
        goto EXIT;
    }

    if(setsockopt(tcpListenFd,SOL_SOCKET,SO_REUSEPORT, &opt, sizeof(int)) != 0)
    {
        logError("fails to setsockopt for SO_REUSEPORT.");
        goto EXIT;
    }

    // Bind the socket with the server address
    if(bind(tcpListenFd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0 )
    {
        logError("tcpListenSocket bind fails, tcpListenfd=%d.", tcpListenFd);
        goto EXIT;
    }

	if(listen(tcpListenFd, 5) != 0) 
	{
		logError("fails to ,listen tcpListenFd (%d), errno=%d.", tcpListenFd, errno);
		goto EXIT;
	}

    event.events = EPOLLIN;
    event.data.fd = tcpListenFd;
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, tcpListenFd, &event))
    {
        logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", udpFd, tpEpFd, errno);
        goto EXIT;
    }

    debug("tcp listening fd =%d (ip=%r, port=%d) is added into epoll fd (%d).", tcpListenFd, &tpSetting.local.ip, tpSetting.local.port, tpEpFd);

	ssize_t len;
	size_t bufLen;
	char* buffer;
	osMBuf_t* udpBuf;
	int event_count;
//    size_t ipcMsgAddr;
	osIPCMsg_t ipcMsg;
    while (1)
    {
        event_count = epoll_wait(tpEpFd, events, SYS_MAX_EPOLL_WAIT_EVENTS, -1);
        for(int i = 0; i < event_count; i++) 
		{
			if(events[i].data.fd != tpSetting.ownIpcFd[0])
			{
				logError("to-remove, fd=%d", events[i].data.fd);
			}
			if(events[i].data.fd == tpSetting.ownIpcFd[0])
			{
            	while (1)
            	{
               		//len = read(events[i].data.fd, &ipcMsgAddr, sizeof(size_t));
                    len = read(events[i].data.fd, &ipcMsg, sizeof(osIPCMsg_t));

               		if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
					{
                   		break;
					}

					//sipTpServerOnIpcMsg((osIPCMsg_t*)ipcMsgAddr);
                    sipTpServerOnIpcMsg(&ipcMsg);
				}
			}
			else if(events[i].data.fd == udpFd)
			{
				while(1)
				{
					struct sockaddr_in peerAddr;
					int peerAddrLen = sizeof(peerAddr);
					udpBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
					len = recvfrom(udpFd, (char *)udpBuf->buf, SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *) &peerAddr, &peerAddrLen);
					if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EBADF))
					{
						debug("EAGAIN received for udpfd(%d).", udpFd);
						break;
					}

					if(len < 10)
					{
						debug("received udp message with len(%d) less than minimum packet size for udp (%d).", len, udpFd);
						continue;
					}

					char *ip = inet_ntoa(peerAddr.sin_addr);
					debug("to-remove, peer ip=%s, port=%d.", ip, ntohs(peerAddr.sin_port));
					udpBuf->end = len;
					sipTpServerForwardMsg(udpBuf, -1, &peerAddr);
                    //sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, udpBuf, 0);
				}
			}
			else if(events[i].data.fd == tcpListenFd)
			{
				struct sockaddr tcpPeer;
				int tcpAddrLen = sizeof(tcpPeer);
				int tcpfd = accept(tcpListenFd, &tcpPeer, &tcpAddrLen);
				if(tcpfd == -1)
				{
					mlogInfo(LM_TRANSPORT, "fails to accept for tcpListenFd(%d), errno=%d.", tcpListenFd, errno);
					continue;
				}

				if(fcntl(tcpfd, F_SETFL, O_NONBLOCK) != 0)
				{
                    mlogInfo(LM_TRANSPORT, "fails to set O_NONBLOCK for tcpListenFd(%d), errno=%d.", tcpListenFd, errno);
                    continue;
                }

				event.events = EPOLLIN | EPOLLRDHUP;
				event.data.fd = tcpfd;
				if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, tcpfd, &event))
    			{
        			mlogInfo(LM_TRANSPORT, "fails to add file descriptor(%d) to epoll(%d), errno=%d.\n", tcpfd, tpEpFd, errno);
        			continue;
    			}

                mlogInfo(LM_TRANSPORT, "accepted a tcp connection, tcpFd=%d", tcpfd);

				//add into tcm
				osStatus_e status = sipTpTcmAddFd(tcpfd, (struct sockaddr_in*) &tcpPeer);
				if(status != OS_STATUS_OK)
				{
					mlogInfo(LM_TRANSPORT, "fails to sipTpTcmAddFd for tcpfd(%d), close this fd.", tcpfd);
					close(tcpfd);
					continue;
				}
			}
			else	//dedicated tcp connection
			{
				if(events[i].events & EPOLLERR)
				{
					close(events[i].data.fd);
                    sipTpDeleteTcm(events[i].data.fd);
					continue;
				}

				//isDumpMsg and dumpBuf are added to prevent situation when pTcm == NULL
				bool isDumpMsg = false;
				char dumbBuf[SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE];
                sipTpTcm_t* pTcm = sipTpGetConnectedTcm(events[i].data.fd);
				if(!pTcm)
				{
					logError("received a TCP message from tcpfd (%d) that does not have TCM.", events[i].data.fd);
					isDumpMsg = true;
				}

				//pSipMsgBuf->pos = the end of processed received bytes 
				//pSipMsgBuf->end = the end of last received bytes
            	while (1) 
				{
					if(isDumpMsg)
					{
						buffer = dumbBuf;
						bufLen = SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE;
					}
					else
					{
               			buffer = &pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->end];
               			bufLen = pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->size - pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->end;
						if(bufLen == 0)
						{
							logError("something is wrong on message received from tcpFd(%d), the message size exceeds the allowed SIP MESSAGE SIZE.", events[i].data.fd);
							sipTpTcmBufInit(&pTcm->tcpKeepAlive.sipBuf, false);
							buffer = pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf;
							bufLen = pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->size;
						}
					}

               		len = recv(events[i].data.fd, (char *)buffer, bufLen, MSG_DONTWAIT);

               		if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
               		{
                   		debug("EAGAIN received\n");
						isDumpMsg = false;
                   		break;
               		}
					else if(len == 0)
					{
						if(events[i].events & EPOLLRDHUP)
						{
							//close the fd
							debug("peer closed the TCP connection for tcpfd (%d).", events[i].data.fd);
							isDumpMsg = false;
							close(events[i].data.fd);
							sipTpDeleteTcm(events[i].data.fd);
						}
						break;
					}

					if(isDumpMsg)
					{
						isDumpMsg = false;
						break;
					}	

					//osPointerLen_t dbgPL={buffer, len};
					//mdebug(LM_TRANSPORT, "received TCP message, len=%d from fd(%d), bufLen=%ld, msg=\n%r", len, events[i].data.fd, bufLen, &dbgPL);

					//basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
					if(pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->pos == 0 && (pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0] < 'A' || pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0] > 'Z'))
					{
						mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0], len);
						if(sipTpSafeGuideMsg(pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf, len))
						{
							continue;
						}
					}
					size_t nextStart = 0;
					ssize_t remaining = sipTpAnalyseMsg(&pTcm->tcpKeepAlive.sipBuf, len, &nextStart);
					if(remaining < 0)
					{
						mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
						continue;
					}
					else
					{
						osMBuf_t* pCurSipBuf = pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf;
						bool isBadMsg = pTcm->tcpKeepAlive.sipBuf.state.isBadMsg;
						//if isBadMsg, drop the current received sip message, and reuse the pSipMsgBuf 	
						if(!sipTpTcmBufInit(&pTcm->tcpKeepAlive.sipBuf, isBadMsg ? false : true))
						{
							logError("fails to init a TCM pSipMsgBuf.");
							goto EXIT;
						}

						//copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
						if(remaining > 0)
						{
							memcpy(pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf, &pCurSipBuf->buf[nextStart], remaining);
						}
						pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->end = remaining;
						pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->pos = 0;

						if(!isBadMsg)
						{
							pTcm->tcpKeepAlive.isUsed = true;

                    		sipTpServerForwardMsg(pCurSipBuf, events[i].data.fd, &pTcm->peer);
						}
						else
						{
							mdebug(LM_TRANSPORT, "received a bad msg, drop.");
						}
					}
				}
        	}
    	}
	}

EXIT:
	logError("a transport thread fails, exiting...");

	if(tpSetting.ownIpcFd >= 0)
	{
        close(tpSetting.ownIpcFd[0]);
	}
	if(udpFd >= 0)
	{
        close(udpFd);
	}

	sipTpDeleteAllTcm();

	if(tpEpFd >= 0)
	{
        close(tpEpFd);
	}
        
	exit(EXIT_FAILURE);
}



sipTransportStatus_e sipTpServer_send(sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf)
{
	osStatus_e status = OS_STATUS_OK;
	sipTransportStatus_e tpStatus = SIP_TRANSPORT_STATUS_TCP_OK;
	ssize_t len;

	if(!pTpInfo || !pSipBuf)
	{
		logError("null pointer, pTpInfo=%p, pSipBuf=%p.", pTpInfo, pSipBuf);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//allow the sending of UDP due to some clients (like IMS client) ignores top via's send_by, insist to send response to the real ip:port used in the request,
	if(pTpInfo->tpType == SIP_TRANSPORT_TYPE_UDP)
	{
        tpStatus = SIP_TRANSPORT_TYPE_UDP;
        if(pTpInfo->viaProtocolPos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "UDP", 3, pTpInfo->viaProtocolPos);
        }

        struct sockaddr_in dest;
        status = sipTpConvertPLton(&pTpInfo->peer, true, &dest);
        if(status != OS_STATUS_OK)
        {
            logError("fails to perform sipTpConvertPLton.");
            goto EXIT;
        }

        logInfo("send a SIP message via UDP FD=%d, dest ip=%s, port=%d, sip message=\n%M", udpFd, inet_ntoa(dest.sin_addr), ntohs(dest.sin_port), pSipBuf);
        len = sendto(udpFd, pSipBuf->buf, pSipBuf->end, 0, (const struct sockaddr*) &dest, sizeof(dest));
//        len = sendto(udpFd, "hello", 5, 0, (const struct sockaddr*) &dest, sizeof(dest));
        if(len != pSipBuf->end || len == -1)
        {
            logError("fails to sendto() for udpFd=%d, len=%d, errno=%d.", udpFd, len, errno);
            status = OS_ERROR_NETWORK_FAILURE;
        }
	
		goto EXIT;
	}

	//for sending via TCP, unless it is a response for a request received vis TCP fd in tpServer, not allow it.
	if(pTpInfo->tpType != SIP_TRANSPORT_TYPE_TCP || pTpInfo->tcpFd == -1)
	{
		logError("try to send a sip message via transport server TCP.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//check if this fd is closed.
    struct sockaddr_in peer = {};
    struct sockaddr_in local = {};

    if(sipTpConvertPLton(&pTpInfo->peer, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to sipTpConvertPLton for peer IP=%r, errno=%d.", pTpInfo->peer.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //first check if there is already a TCP connection exists
    sipTpTcm_t* pTcm = sipTpGetTcmByFd(pTpInfo->tcpFd, peer);
	if(!pTcm)
	{
		mlogInfo(LM_TRANSPORT, "the tcpfd(%d, peer %r:%d) has been closed in the tpServer.", pTpInfo->tcpFd, &pTpInfo->peer.ip, pTpInfo->peer.port);
		sipTpListUsedTcm();
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	int sockfd;
	len = write(pTpInfo->tcpFd, pSipBuf->buf, pSipBuf->end);
	if(len < 0 || len != pSipBuf->end)
	{
		logError("fails to send TCP out, fd=%d, len=%d, msgSize=%ld.", pTpInfo->tcpFd, len, pSipBuf->end);
		status = OS_ERROR_NETWORK_FAILURE;
	}

    if(pTpInfo->viaProtocolPos != 0)
	{	
		osMBuf_modifyStr(pSipBuf, "TCP", 3, pTpInfo->viaProtocolPos);
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		tpStatus = SIP_TRANSPORT_STATUS_FAIL;
	}

	return tpStatus;
}


static void sipTpServerOnIpcMsg(osIPCMsg_t* pIpcMsg)
{
	switch (pIpcMsg->interface)
	{
		case OS_TIMER_ALL:
		case OS_TIMER_TICK:
			osTimerGetMsg(pIpcMsg->interface, pIpcMsg->pMsg);
			break;
		case OS_SIP_TRANSPORT_LBINFO:
			sipTpServerUpdateLBInfo(pIpcMsg->pMsg);
			break;
		default:
			logError("received ipc message from unknown interface (%d).", pIpcMsg->interface);
			break;
	}
}


static void sipTpServerForwardMsg(osMBuf_t* pSipBuf, int tcpFd, struct sockaddr_in* peer)
{
    osIPCMsg_t ipcMsg;
    ipcMsg.interface = OS_SIP_TRANSPORT_SERVER;

	sipTransportMsgBuf_t* pMsg = osMem_alloc(sizeof(sipTransportMsgBuf_t), sipTransportMsgBuf_free);
    pMsg->pSipBuf = pSipBuf;
    pMsg->tcpFd = tcpFd;
	pMsg->isServer = true;
	if(peer)
	{
		pMsg->peer = *peer;
	}
//    pMsg->tpId = pTcm;
    ipcMsg.pMsg = (void*) pMsg;
	debug("to-remove, (sipTransportMsgBuf_t*)pMsg=%p.", pMsg);

#if 0
	pSipBuf->buf[pSipBuf->end] = '\0';
	for(int i=0; i<pSipBuf->end; i++)
	{
		debug("i=%d, msg=0x%x.", i, pSipBuf->buf[i]);
	}
#endif
	char ip[INET_ADDRSTRLEN]={};
	inet_ntop(AF_INET, &pMsg->peer.sin_addr, ip, INET_ADDRSTRLEN);
	mlogInfo(LM_TRANSPORT, "received a sip Msg from %s:%d, the msg=\n%M", ip, ntohs(pMsg->peer.sin_port), pSipBuf);

	//to-do, need to go to hash table, to find the destination ipc id, for now, just forward to the first one.
    write(lbFd[0], (void*) &ipcMsg, sizeof(osIPCMsg_t));
}

static void sipTpServerUpdateLBInfo(void* pMsg)
{
	//to-do
	//add, remove LB info
}


static void sipTpServerTimeout(uint64_t timerId, void* ptr)
{
    logInfo("received timeout for timerId=%d.", timerId);
}
	
