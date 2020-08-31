/* Copyright 2020, 2019, Sean Dai
 * implement tcp and udp listening as a stand alone thread.
 */


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
#include "osHexDump.h"

#include "sipConfig.h"
#include "sipMsgFirstLine.h"
#include "sipUriparam.h"
#include "sipTransMgr.h"
#include "sipTransportIntf.h"
#include "transportCom.h"
#include "tcm.h"
#include "transportLib.h"
#include "diaTransportIntf.h"


static void sipTpServerOnIpcMsg(osIPCMsg_t* pIpcMsg);
static void sipTpServerTimeout(uint64_t timerId, void* ptr);
//static void sipTpServerForwardMsg(osMBuf_t* pSipBuf, int tcpFd, struct sockaddr_in* peer);
static void tpServerForwardMsg(transportAppType_e appType, osMBuf_t* pBuf, int tcpFd, struct sockaddr_in* peer);
static void sipTpServerUpdateLBInfo(void* pMsg);
static int sipTpSetTcpListener(transportIpPort_t* pIpPort);
//static osStatus_e tpProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len);
static bool tpIsListenFd(int fd, transportAppType_e* appType);
static transportStatus_e tpCreateAndSendTcp(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd);
static sipTransportStatus_e tpConnectedTcpSend(transportInfo_t* pTpInfo, osMBuf_t* pMsgBuf);
static sipTransportStatus_e tpUdpSend(transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pMsgBuf);
//static sipTransportStatus_e tpServerCreateTcpConn(transportAppType_e appType, void* pAppId, struct sockaddr_in* peer, struct sockaddr_in* local, sipTransportStatus_e* pTransport);


//static __thread transportMainSetting_t tpSetting;
static __thread osList_t tpListenerList;
static int tpEpFd=-1;
static __thread osHash_t* lbHash;
static __thread int lbFd[SIP_CONFIG_TRANSACTION_THREAD_NUM];
static __thread osHash_t* sipTransportLBHash;
//can not be __thread since it may be used by transaction layer directly for sending message via UDP
static int sipUdpFd=-1;

osStatus_e transportMainInit(int pipefd[2], uint32_t bucketSize)
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


void* transportMainStart(void* pData)
{
	struct sockaddr_in localAddr;
	struct epoll_event event, events[SYS_MAX_EPOLL_WAIT_EVENTS];

    transportMainSetting_t tpSetting = *(transportMainSetting_t*)pData;
	osfree(pData);

    osTimerInit(tpSetting.timerfd, tpSetting.ownIpcFd[1], SIP_CONFIG_TIMEOUT_MULTIPLE, sipTpServerTimeout);

	for(int i=0; i<SIP_CONFIG_TRANSACTION_THREAD_NUM; i++)
	{
		lbFd[i] = tpSetting.lbFd[i];
	}

    debug("threadId = 0x%lx.", pthread_self());

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

    debug("ownIpcFd(%d) is added into epoll fd (%d).", tpSetting.ownIpcFd[0], tpEpFd);

    memset(&localAddr, 0, sizeof(localAddr));
//    memset(&peerAddr, 0, sizeof(peerAddr));

	if(tpConvertPLton(&tpSetting.udpLocal, true, &localAddr) != OS_STATUS_OK)
	{
		logError("fails to tpConvertPLton for udp, IP=%r, port=%d.", &tpSetting.udpLocal.ip, tpSetting.udpLocal.port);
		goto EXIT;
	}

	//create UDP listening socket
    if((sipUdpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0 ) 
	{ 
		logError("fails to create UDP socket.");
		goto EXIT;
    } 

	int opt = 1;
	//has to set SO_REUSEADDR, otherwise, bind() will get EADDRINUSE(98) error when port is specified
	if(setsockopt(sipUdpFd,SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) != 0)
	{
        logError("fails to setsockopt for SO_REUSEADDR.");
        goto EXIT;
    }

	if(setsockopt(sipUdpFd,SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) != 0) 
	{
        logError("fails to setsockopt for SO_REUSEPORT.");
        goto EXIT;
    }
   
    // Bind the socket with the server address 
    if(bind(sipUdpFd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0) 
    { 
        logError("udpSocket bind fails, localIP=%r, udpSocketfd=%d, errno=%d.", &tpSetting.udpLocal.ip, sipUdpFd, errno); 
		goto EXIT;
    } 

    event.events = EPOLLIN;
    event.data.fd = sipUdpFd;
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, sipUdpFd, &event))
    {
        logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", sipUdpFd, tpEpFd, errno);
        goto EXIT;
    }

	debug("udp listening fd =%d (ip=%r, port=%d) is added into epoll fd (%d).", sipUdpFd, &tpSetting.udpLocal.ip, tpSetting.udpLocal.port, tpEpFd);

    //create TCP listening socket
	for(int i=0; i< tpSetting.tcpInfoNum; i++)
	{
		int tcpListenFd = sipTpSetTcpListener(&tpSetting.serverTcpInfo[i].local);
		if(tcpListenFd < 0)
		{
			logError("fails to create TCP listener for (%r:%d).", &tpSetting.serverTcpInfo[i].local.ip, tpSetting.serverTcpInfo[i].local.port);
			goto EXIT;
		}

		tpListenerInfo_t* pAppListener = osmalloc(sizeof(tpListenerInfo_t), NULL);
		if(!pAppListener)
		{
			logError("fails to osmalloc for pAppListener, fd=%d, appType=%d.", tcpListenFd, tpSetting.serverTcpInfo[i].appType);
			goto EXIT;
		}

		pAppListener->tcpFd = tcpListenFd;
		pAppListener->appType = tpSetting.serverTcpInfo[i].appType;
		osList_append(&tpListenerList, pAppListener);

    	debug("tcp listening fd =%d (ip=%r, port=%d, appType=%d) is added into epoll fd (%d).", tcpListenFd, &tpSetting.serverTcpInfo[i].local.ip, tpSetting.serverTcpInfo[i].local.port, tpSetting.serverTcpInfo[i].appType, tpEpFd);
	}

	transportAppType_e appType;
	ssize_t len;
	size_t bufLen;
	char* buffer;
	osMBuf_t* udpBuf;
    char udpRcv[SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE];	//temp hold the received message to avoid allc/dealloc udp buffer when receiving ping or status info
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
				logInfo("received a socket message, fd=%d", events[i].data.fd);
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
			else if(events[i].data.fd == sipUdpFd)
			{
				//char* udpRcv[SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE];
				while(1)
				{
					struct sockaddr_in peerAddr;
					int peerAddrLen = sizeof(peerAddr);
				//	udpBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
					len = recvfrom(sipUdpFd, udpRcv, SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *) &peerAddr, &peerAddrLen);
					if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EBADF))
					{
						debug("EAGAIN received for udpfd(%d).", sipUdpFd);
						break;
					}

					if(len < 10)
					{
						debug("received udp message with len(%d) less than minimum packet size for udp (%d).", len, sipUdpFd);
						continue;
					}

                    udpBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
					memcpy(udpBuf->buf, udpRcv, len);

					//char *ip = inet_ntoa(peerAddr.sin_addr);
					debug("to-remove, peer=%A.", &peerAddr);
					udpBuf->end = len;
					tpServerForwardMsg(TRANSPORT_APP_TYPE_SIP, udpBuf, -1, &peerAddr);
                    //sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, udpBuf, 0);
				}
			}
//			else if(events[i].data.fd == tcpListenFd)
			else if(tpIsListenFd(events[i].data.fd, &appType))
			{
				struct sockaddr tcpPeer;
				int tcpAddrLen = sizeof(tcpPeer);
				int tcpfd = accept(events[i].data.fd, &tcpPeer, &tcpAddrLen);
				if(tcpfd == -1)
				{
					mlogInfo(LM_TRANSPORT, "fails to accept for tcpListenFd(%d), errno=%d.", events[i].data.fd, errno);
					continue;
				}

				if(fcntl(tcpfd, F_SETFL, O_NONBLOCK) != 0)
				{
                    mlogInfo(LM_TRANSPORT, "fails to set O_NONBLOCK for tcpListenFd(%d), errno=%d.", events[i].data.fd, errno);
                    continue;
                }

				event.events = EPOLLIN | EPOLLRDHUP;
				event.data.fd = tcpfd;
				if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, tcpfd, &event))
    			{
        			mlogInfo(LM_TRANSPORT, "fails to add file descriptor(%d) to epoll(%d), errno=%d.\n", tcpfd, tpEpFd, errno);
        			continue;
    			}

                mlogInfo(LM_TRANSPORT, "accepted a tcp connection, tcpFd=%d, added into epoll fd(%d)", tcpfd, tpEpFd);

				//add into tcm
				osStatus_e status = tpTcmAddFd(tcpfd, (struct sockaddr_in*) &tcpPeer, appType);
				if(status != OS_STATUS_OK)
				{
					mlogInfo(LM_TRANSPORT, "fails to tpTcmAddFd for tcpfd(%d), appType(%d), close this fd.", tcpfd, appType);
					close(tcpfd);
					continue;
				}
			}
			else	//dedicated tcp connection
			{
				//if waiting for TCP connection establishment for a locally initiated connection
                if(events[i].events & EPOLLOUT)
                {
					//if was found that events may return 0x14, 0x201c, and tcp connect() may still return properly.  Also, return from epoll_wait() may be before or after connect() return() (probably due to multi threads).  EPOLLHUP means th assigned fd is not useable, probably due to previous close has not completely done in the OS due to underneith TCP mechanism.  Waiting a little bit, EPOLLHUP shall be cleared for the fd by the OS.  it is observed that when I killed an app by doing ctrl-c, then restarts the app, the same fd will be returned after the app called socket().  but when performing connect() using the reassigned fd, EPOLLHUP may return.  waiting a bit to redo, EPOLLHUP would not be returned   
                    if(events[i].events & (EPOLLERR|EPOLLHUP))
                    {
						debug("received event=0x%x, close the connection (fd=%d), notifying app.", events[i].events, events[i].data.fd);
						tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
                        //close(events[i].data.fd);	
                        //tpDeleteTcm(events[i].data.fd);
                    }
                    else
                    {
                        debug("fd(%d) is conncted, events=0x%x.", events[i].data.fd, events[i].events);

                        event.events = EPOLLIN | EPOLLRDHUP;
                        event.data.fd = events[i].data.fd;
                        epoll_ctl(tpEpFd, EPOLL_CTL_MOD, events[i].data.fd, &event);

                        tpTcmUpdateConn(events[i].data.fd, true);
                    }
                    continue;
                }

				//for all other already established TCP connections
				if(events[i].events & EPOLLERR)
				{
					tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
					continue;
				}

				//dumpBuf is added to prevent situation when pTcm == NULL
				char dumbBuf[SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE];
                tpTcm_t* pTcm = tpGetConnectedTcm(events[i].data.fd);
				if(!pTcm)
				{
					logError("received a TCP message from tcpfd (%d) that does not have TCM.", events[i].data.fd);
					appType = TRANSPORT_APP_TYPE_UNKNOWN;
				}
				else
				{
					appType = pTcm->appType;
				}
			
				osMBuf_t* aMsg = NULL;	
				//pSipMsgBuf->pos = the end of processed received bytes 
				//pSipMsgBuf->end = the end of last received bytes
            	while (1) 
				{
					switch(pTcm->appType)
					{
	                    case TRANSPORT_APP_TYPE_SIP:
						case TRANSPORT_APP_TYPE_DIAMETER:
    	                    buffer = &pTcm->msgConnInfo.pMsgBuf->buf[pTcm->msgConnInfo.pMsgBuf->end];
        	                bufLen = pTcm->msgConnInfo.pMsgBuf->size - pTcm->msgConnInfo.pMsgBuf->end;
            	            if(bufLen == 0)
                	        {
                    	        logError("something is wrong on message received from tcpFd(%d), the message size exceeds the allowed MESSAGE SIZE.", events[i].data.fd);
                        	    tpTcmBufInit(pTcm, false);
                            	buffer = pTcm->msgConnInfo.pMsgBuf->buf;
                            	bufLen = pTcm->msgConnInfo.pMsgBuf->size;
                        	}
							aMsg = pTcm->msgConnInfo.pMsgBuf;
							break;
                    	case TRANSPORT_APP_TYPE_UNKNOWN:
							buffer = dumbBuf;
							bufLen = SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE;
							break;
					}

               		len = recv(events[i].data.fd, (char *)buffer, bufLen, MSG_DONTWAIT);

               		if(len == -1)
					{
						if(errno == EWOULDBLOCK || errno == EAGAIN)
               			{
                   			debug("EAGAIN received\n");
                   			break;
               			}
						else
						{
							//it was noticed that len=-1 and errno = 14 (EFAULT) was received
							logError("len=-1, errno=%d.", errno);
	                        tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
		                    //close(events[i].data.fd);
        		            //tpDeleteTcm(events[i].data.fd);

							break;
						}
					}
					else if(len == 0)
					{
						if(events[i].events & EPOLLRDHUP)
						{
							//close the fd
							debug("peer closed the TCP connection for tcpfd (%d).", events[i].data.fd);
                            tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
							//close(events[i].data.fd);
							//tpDeleteTcm(events[i].data.fd);
						}
						break;
					}

					bool isForwardMsg = false;
					debug("received message for appType=%d, len=%d.", pTcm->appType, len);
					switch(pTcm->appType)
					{
                        case TRANSPORT_APP_TYPE_SIP:
							if(tpProcessSipMsg(pTcm, events[i].data.fd, len, &isForwardMsg) != OS_STATUS_OK)
							{
								goto EXIT;
							}
                            break;
                        case TRANSPORT_APP_TYPE_DIAMETER:
							if(tpProcessDiaMsg(pTcm, events[i].data.fd, len, &isForwardMsg) != OS_STATUS_OK)
                            {
                                goto EXIT;
                            }
                            break;
                        case TRANSPORT_APP_TYPE_UNKNOWN:
							break;
					}	

					if(isForwardMsg)
					{
                    	int tcpFd = events[i].data.fd;
                    	tpServerForwardMsg(pTcm->appType, aMsg, tcpFd, &pTcm->peer);	
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
	if(sipUdpFd >= 0)
	{
        close(sipUdpFd);
	}

	tpDeleteAllTcm();

	if(tpEpFd >= 0)
	{
        close(tpEpFd);
	}
        
	exit(EXIT_FAILURE);
}


transportStatus_e com_send(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd)
{
	transportStatus_e tpStatus = TRANSPORT_STATUS_TCP_OK;

	switch(pTpInfo->tpType)
	{
		case TRANSPORT_TYPE_UDP:
			tpStatus = tpUdpSend(appType, pTpInfo, pBuf);
			break;
		case TRANSPORT_TYPE_TCP:
			if(pTpInfo->tcpFd >= 0)
			{
				tpStatus = tpConnectedTcpSend(pTpInfo, pBuf);
			}
			else
			{
				tpStatus = tpCreateAndSendTcp(appType, appId, pTpInfo, pBuf, fd);
			}
			break;
		default:
			logError("unexpected type(%d).", pTpInfo->tpType);
			break;
	}

	return tpStatus;
}


//this function currently only sends TCP for already established TCP and sip UDP.  For UDP for other protocols, yet to do.
sipTransportStatus_e tpConnectedTcpSend(transportInfo_t* pTpInfo, osMBuf_t* pMsgBuf)
{
	osStatus_e status = OS_STATUS_OK;
	sipTransportStatus_e tpStatus = TRANSPORT_STATUS_TCP_OK;
	ssize_t len;

	if(!pTpInfo || !pMsgBuf)
	{
		logError("null pointer, pTpInfo=%p, pMsgBuf=%p.", pTpInfo, pMsgBuf);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

#if 0
	//allow the sending of UDP due to some clients (like IMS client) ignores top via's send_by, insist to send response to the real ip:port used in the request,
	if(pTpInfo->tpType == TRANSPORT_TYPE_UDP)
	{
        tpStatus = TRANSPORT_TYPE_UDP;
        if(pTpInfo->protocolUpdatePos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "UDP", 3, pTpInfo->protocolUpdatePos);
        }

        struct sockaddr_in dest;
        status = tpConvertPLton(&pTpInfo->peer, true, &dest);
        if(status != OS_STATUS_OK)
        {
            logError("fails to perform tpConvertPLton.");
            goto EXIT;
        }

        logInfo("send a SIP message via UDP FD=%d, dest ip=%s, port=%d, sip message=\n%M", sipUdpFd, inet_ntoa(dest.sin_addr), ntohs(dest.sin_port), pMsgBuf);
        len = sendto(sipUdpFd, pMsgBuf->buf, pMsgBuf->end, 0, (const struct sockaddr*) &dest, sizeof(dest));
//        len = sendto(sipUdpFd, "hello", 5, 0, (const struct sockaddr*) &dest, sizeof(dest));
        if(len != pMsgBuf->end || len == -1)
        {
            logError("fails to sendto() for sipUdpFd=%d, len=%d, errno=%d.", sipUdpFd, len, errno);
            status = OS_ERROR_NETWORK_FAILURE;
        }
	
		goto EXIT;
	}
#endif
	//for sending via TCP, unless it is a response for a request received vis TCP fd in tpServer, not allow it.
	if(pTpInfo->tpType != TRANSPORT_TYPE_TCP || pTpInfo->tcpFd == -1)
	{
		logError("try to send a message via transport server TCP.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//check if this fd is closed.
#if 0	//use network address
    struct sockaddr_in peer = {};
    struct sockaddr_in local = {};

    if(tpConvertPLton(&pTpInfo->peer, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for peer IP=%r, errno=%d.", pTpInfo->peer.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcmByFd(pTpInfo->tcpFd, peer);
	if(!pTcm)
	{
		mlogInfo(LM_TRANSPORT, "the tcpfd(%d, peer %r:%d) has been closed in the tpServer.", pTpInfo->tcpFd, &pTpInfo->peer.ip, pTpInfo->peer.port);
		tpListUsedTcm();
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

#else
    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcmByFd(pTpInfo->tcpFd, pTpInfo->peer);
    if(!pTcm)
    {
        mlogInfo(LM_TRANSPORT, "the tcpfd(%d, peer %A) has been closed in the tpServer.", pTpInfo->tcpFd, &pTpInfo->peer);
        tpListUsedTcm();
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
#endif


	int sockfd;
	len = write(pTpInfo->tcpFd, pMsgBuf->buf, pMsgBuf->end);
	if(len < 0 || len != pMsgBuf->end)
	{
		logError("fails to send TCP out, fd=%d, len=%d, msgSize=%ld.", pTpInfo->tcpFd, len, pMsgBuf->end);
		status = OS_ERROR_NETWORK_FAILURE;
	}

    if(pTpInfo->protocolUpdatePos != 0)
	{	
		osMBuf_modifyStr(pMsgBuf, "TCP", 3, pTpInfo->protocolUpdatePos);
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		tpStatus = TRANSPORT_STATUS_FAIL;
	}

	return tpStatus;
}


static sipTransportStatus_e tpUdpSend(transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pMsgBuf)
{
    osStatus_e status = OS_STATUS_OK;
    sipTransportStatus_e tpStatus = TRANSPORT_TYPE_UDP;
    ssize_t len;

    if(!pTpInfo || !pMsgBuf)
    {
        logError("null pointer, pTpInfo=%p, pMsgBuf=%p.", pTpInfo, pMsgBuf);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(appType != TRANSPORT_APP_TYPE_SIP)
	{
		logInfo("appType(%d) is not allowed to use UDP.", appType);
		tpStatus = TRANSPORT_STATUS_FAIL;
	}

	//for now, only allow sip to use UDP
    //allow the sending of UDP due to some clients (like IMS client) ignores top via's send_by, insist to send response to the real ip:port used in the request,
    if(pTpInfo->protocolUpdatePos != 0)
    {
        osMBuf_modifyStr(pMsgBuf, "UDP", 3, pTpInfo->protocolUpdatePos);
    }

#if 0	//use network address
    struct sockaddr_in dest;
    status = tpConvertPLton(&pTpInfo->peer, true, &dest);
    if(status != OS_STATUS_OK)
    {
        logError("fails to perform tpConvertPLton.");
        goto EXIT;
    }
#endif
    logInfo("send a SIP message via UDP FD=%d, dest addr=%A, sip message=\n%M", sipUdpFd, &pTpInfo->peer, pMsgBuf);
    len = sendto(sipUdpFd, pMsgBuf->buf, pMsgBuf->end, 0, (const struct sockaddr*) &pTpInfo->peer, sizeof(struct sockaddr_in));
//        len = sendto(sipUdpFd, "hello", 5, 0, (const struct sockaddr*) &dest, sizeof(dest));
    if(len != pMsgBuf->end || len == -1)
    {
        logError("fails to sendto() for sipUdpFd=%d, len=%d, errno=%d.", sipUdpFd, len, errno);
        status = OS_ERROR_NETWORK_FAILURE;
    }

    goto EXIT;

EXIT:
    if(status != OS_STATUS_OK)
    {
        tpStatus = TRANSPORT_STATUS_FAIL;
    }

    return tpStatus;
}

static void sipTpServerOnIpcMsg(osIPCMsg_t* pIpcMsg)
{
	switch (pIpcMsg->interface)
	{
		case OS_TIMER_ALL:
		case OS_TIMER_TICK:
			osTimerGetMsg(pIpcMsg->interface, pIpcMsg->pMsg, NULL);
			break;
		case OS_SIP_TRANSPORT_LBINFO:
			sipTpServerUpdateLBInfo(pIpcMsg->pMsg);
			break;
		default:
			logError("received ipc message from unknown interface (%d).", pIpcMsg->interface);
			break;
	}
}



//need to rework when working on lb module
int getLbFd()
{
	return lbFd[0];
}

#if 0 
osStatus_e com_closeTcpConn(int tcpFd, bool isNotifyApp)
{
	DEBUG_BEGIN

	osStatus_e status = tpDeleteTcm(tcpFd, isNotifyApp);
	if(status  == OS_STATUS_OK)
	{
		debug("tcpFd=%d is closed.", tcpFd);
    	struct epoll_event event;
    	if(epoll_ctl(tpEpFd, EPOLL_CTL_DEL, tcpFd, &event))
    	{
        	logError("fails to delete file descriptor (%d) to epoll(%d), errno=%d.", tcpFd, tpEpFd, errno);
			status = OS_ERROR_SYSTEM_FAILURE;
        	goto EXIT;
    	}
		if(close(tcpFd) == -1)
		{
			logError("fails to close fd(%d), errno=%d.", tpEpFd, errno);
            status = OS_ERROR_SYSTEM_FAILURE;
			goto EXIT;
		}
	}

EXIT:
	DEBUG_END
	return status;
}
#endif

#if 0
static void sipTpServerForwardMsg(osMBuf_t* pSipBuf, int tcpFd, struct sockaddr_in* peer)
{
    osIPCMsg_t ipcMsg;
    ipcMsg.interface = OS_SIP_TRANSPORT_SERVER;

	sipTransportMsgBuf_t* pMsg = osmalloc(sizeof(sipTransportMsgBuf_t), sipTransportMsgBuf_free);
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
#endif


static void tpServerForwardMsg(transportAppType_e appType, osMBuf_t* pBuf, int tcpFd, struct sockaddr_in* peer)
{
    osIPCMsg_t ipcMsg;
	//sipTransportMsgBuf_t* pMsg;
	void* pMsg;
	
	switch(appType)
	{
		case TRANSPORT_APP_TYPE_SIP:
			ipcMsg.interface = OS_SIP_TRANSPORT_SERVER;
			pMsg = oszalloc(sizeof(sipTransportMsgBuf_t), sipTransportMsgBuf_free);
			sipTransportMsgBuf_t* pSipMsg = pMsg;
		    pSipMsg->pSipBuf = pBuf;
    		pSipMsg->tcpFd = tcpFd;
    		pSipMsg->isCom = true;
    		if(peer)
    		{
        		pSipMsg->peer = *peer;
    		}
			break;
		case TRANSPORT_APP_TYPE_DIAMETER:
			ipcMsg.interface = OS_DIA_TRANSPORT;
			pMsg = oszalloc(sizeof(diaTransportMsg_t), NULL);
			diaTransportMsg_t* pDiaMsg = pMsg;
			pDiaMsg->peerMsg.pDiaBuf = pBuf;
            pDiaMsg->peerMsg.tcpFd = tcpFd;
            if(peer)
            {
                pDiaMsg->peer = *peer;
            }
			break;	
        case TRANSPORT_APP_TYPE_UNKNOWN:
			return;
			break;
	}

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
    inet_ntop(AF_INET, &peer->sin_addr, ip, INET_ADDRSTRLEN);
	if(appType == TRANSPORT_APP_TYPE_DIAMETER)
	{
		mlogInfo(LM_TRANSPORT, "received a Msg from %s:%d for app type(%d), the msg=", ip, ntohs(peer->sin_port), appType);
		hexdump(stdout, pBuf->buf, pBuf->end);
	}
	else
	{ 
    	mlogInfo(LM_TRANSPORT, "received a Msg from %s:%d for app type(%d), the msg=\n%M", ip, ntohs(peer->sin_port), appType, pBuf);
	}
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
	


static int sipTpSetTcpListener(transportIpPort_t* pIpPort)
{
	int tcpListenFd = -1;
	struct sockaddr_in localAddr;

    memset(&localAddr, 0, sizeof(localAddr));

    if(tpConvertPLton(pIpPort, true, &localAddr) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for TCP, IP=%r, port=%d.", &pIpPort->ip, pIpPort->port);
        goto EXIT;
    }

    //create TCP listening socket
    if((tcpListenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        logError("fails to create TCP socket.");
        goto EXIT;
    }

	int opt = 1;
    if(setsockopt(tcpListenFd,SOL_SOCKET,SO_REUSEPORT, &opt, sizeof(int)) != 0)
    {
        logError("fails to setsockopt for SO_REUSEPORT.");
		tcpListenFd = -1;
        goto EXIT;
    }

    // Bind the socket with the server address
    if(bind(tcpListenFd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0 )
    {
        logError("tcpListenSocket bind fails, tcpListenfd=%d.", tcpListenFd);
		tcpListenFd = -1;
        goto EXIT;
    }

    if(listen(tcpListenFd, 5) != 0)
    {
        logError("fails to ,listen tcpListenFd (%d), errno=%d.", tcpListenFd, errno);
		tcpListenFd = -1;
        goto EXIT;
    }

    struct epoll_event event;
	event.events = EPOLLIN;
    event.data.fd = tcpListenFd;
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, tcpListenFd, &event))
    {
        logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", sipUdpFd, tpEpFd, errno);
		tcpListenFd = -1;
        goto EXIT;
    }

    debug("tcpListenFd(%d) is added into epoll fd (%d).", tcpListenFd, tpEpFd);

EXIT:
	return tcpListenFd;
}


#if 0
static osStatus_e tpProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len)
{
	osStatus_e status = OS_STATUS_OK;

	//mdebug(LM_TRANSPORT, "received TCP message, len=%d from fd(%d), bufLen=%ld, msg=\n%r", len, events[i].data.fd, bufLen, &dbgPL);
	//basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
    if(pTcm->msgConnInfo.pMsgBuf->pos == 0 && (pTcm->msgConnInfo.pMsgBuf->buf[0] < 'A' || pTcm->msgConnInfo.pMsgBuf->buf[0] > 'Z'))
    {
    	mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->msgConnInfo.pMsgBuf->buf[0], len);
        if(sipTpSafeGuideMsg(pTcm->msgConnInfo.pMsgBuf, len))
      	{
          	goto EXIT;
        }
    }
    size_t nextStart = 0;
    ssize_t remaining = sipTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.state, len, &nextStart);
    if(remaining < 0)
    {
     	mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
        goto EXIT;
    }
    else
    {
    	osMBuf_t* pCurSipBuf = pTcm->msgConnInfo.pMsgBuf;
        bool isBadMsg = pTcm->msgConnInfo.state.isBadMsg;
        //if isBadMsg, drop the current received sip message, and reuse the pSipMsgBuf
        if(!tpTcmBufInit(pTcm, isBadMsg ? false : true))
        {
          	logError("fails to init a TCM pSipMsgBuf.");
			status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }

        //copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
        if(remaining > 0)
        {
          	memcpy(pTcm->msgConnInfo.pMsgBuf->buf, &pCurSipBuf->buf[nextStart], remaining);
        }
        pTcm->msgConnInfo.pMsgBuf->end = remaining;
        pTcm->msgConnInfo.pMsgBuf->pos = 0;

        if(!isBadMsg)
        {
         	pTcm->msgConnInfo.isUsed = true;

            tpServerForwardMsg(TRANSPORT_APP_TYPE_SIP, pCurSipBuf, tcpFd, &pTcm->peer);
        }
        else
        {
         	mdebug(LM_TRANSPORT, "received a bad msg, drop.");
        }
	}

EXIT:
	return status;
}
#endif

#if 0
static osStatus_e tpProcessDiaMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len)
{
    osStatus_e status = OS_STATUS_OK;

#if 0
    //mdebug(LM_TRANSPORT, "received TCP message, len=%d from fd(%d), bufLen=%ld, msg=\n%r", len, events[i].data.fd, bufLen, &dbgPL);
    //basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
    if(pTcm->msgConnInfo.pMsgBuf->pos == 0 && (pTcm->msgConnInfo.pMsgBuf->buf[0] < 'A' || pTcm->msgConnInfo.pMsgBuf->buf[0] > 'Z'))
    {
        mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->msgConnInfo.pMsgBuf->buf[0], len);
        if(sipTpSafeGuideMsg(pTcm->msgConnInfo.pMsgBuf, len))
        {
            goto EXIT;
        }
    }
#endif

    size_t nextStart = 0;
	
    ssize_t remaining = 0;
	while((remaining = diaTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.diaTcpInfo.state, len, &nextStart)) >= 0)
	{
		//remaining <0 when the pBuf only contins part of a message, that does not necessary the message is bigger than pBuf->size, rather the whole message has not been received, pBuf only partly filled
#if 0
    if(remaining < 0)
    {
        mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
        goto EXIT;
    }
    else
    {
#endif
        osMBuf_t* pCurDiaBuf = pTcm->msgConnInfo.diaTcpInfo.pdiaMsgBuf;
        bool isBadMsg = pTcm->msgConnInfo.diaTcpInfo.state.isBadMsg;
        //if isBadMsg, drop the current received dia message, and reuse the pDiaMsgBuf
        if(!tpTcmBufInit(pTcm, isBadMsg ? false : true))
        {
            logError("fails to init a TCM pDiaMsgBuf.");
            status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }

        //copy the next sip message pieces that have been read into the newly allocated pSipMsgBuf
        if(remaining > 0)
        {
            memcpy(pTcm->msgConnInfo.pMsgBuf->buf, &pCurDiaBuf->buf[nextStart], remaining);
        	pTcm->msgConnInfo.pMsgBuf->end = remaining;
        	pTcm->msgConnInfo.pMsgBuf->pos = 0;
		}

        if(!isBadMsg)
        {
            pTcm->diaTcpInfo.isUsed = true;

            tpServerForwardMsg(TRANSPORT_APP_TYPE_SIP, pCurSipBuf, tcpFd, &pTcm->peer);
        }
        else
        {
            mdebug(LM_TRANSPORT, "received a bad msg, drop.");
			break;
        }

		//no left over
		if(remaining == 0)
		{
			break;
		}
    }

EXIT:
    return status;
}
#endif

static bool tpIsListenFd(int fd, transportAppType_e* appType)
{
	osListElement_t* pLE = tpListenerList.head;
	while(pLE)
	{
		if(((tpListenerInfo_t*)pLE->data)->tcpFd == fd)
		{
			*appType = ((tpListenerInfo_t*)pLE->data)->appType;
			return true;
		}

		pLE = pLE->next;
	}

	*appType = TRANSPORT_APP_TYPE_UNKNOWN;
	return false;
}


#if 0
//app requests to create a TCP connection
static sipTransportStatus_e tpServerCreateTcpConn(transportAppType_e appType, void* pAppId, struct sockaddr_in* peer, struct sockaddr_in* local, sipTransportStatus_e* pTransport)
{
	sipTransportStatus_e tpStatus = TRANSPORT_STATUS_TCP_FAIL;
    osStatus_e status = OS_STATUS_OK;

#if 0	//use network address
    struct sockaddr_in peer = {};
#if 0
    struct sockaddr_in local = {};

    //do not bind port, use ephemeral port.
    if(tpConvertPLton(localAddr, false, &local) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for local IP=%r, errno=%d.", localAddr->ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
#endif
    if(tpConvertPLton(peerAddr, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for peer IP=%r, errno=%d.", peerAddr->ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
#endif
    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcm(*peer, appType, true);
    if(!pTcm)
    {
        logError("fails to tpGetTcm for peer(%A).", &peer->sin_addr);
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

    if(pTcm->sockfd == -1)
    {
        //check if the peer under quarantine
        if(tpIsInQList(*peer))
        {
	        status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }

		int tcpConnStatus;
        status = tpCreateTcp(tpEpFd, peer, local, &pTcm->sockfd, &tcpConnStatus);
        if(status != OS_STATUS_OK)
        {
			tpReleaseTcm(pTcm);
            goto EXIT;
        }

    	if(tcpConnStatus == 0)
    	{
        	pTcm->isTcpConnDone = true;
    	}
    	else
    	{
        	pTcm->isTcpConnDone = false;
        	osListPlus_append(&pTcm->tcpConn.appIdList, pAppId);
logError("to-remove, TCM2, appId=%p", pAppId);
    	}
//  pTcm->tcpConn.tcpConnTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_TIMEOUT, sipTransport_onTimeout, tcpMapElement);

    	mdebug(LM_TRANSPORT, "sockfd(%d) connStatus=%d, pTcm=%p", pTcm->sockfd, tcpConnStatus, pTcm);
    }

	if(pTcm->isTcpConnDone)
	{
        tpStatus = TRANSPORT_STATUS_TCP_OK;
	}
	else
	{
        tpStatus = TRANSPORT_STATUS_TCP_CONN;
	}

    //for application like diameter, calling tpTcmAddUser() I think is not necessary
	tpTcmAddUser(pTcm, pAppId);

EXIT:
    return status;
}
#endif

static transportStatus_e tpCreateAndSendTcp(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd)
{
    transportStatus_e tpStatus = TRANSPORT_STATUS_TCP_FAIL;

#if 0	//use network address
    struct sockaddr_in peer = {};

    if(tpConvertPLton(&pTpInfo->peer, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for peer IP=%r, errno=%d.", pTpInfo->peer.ip, errno);
        goto EXIT;
    }

    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcm(peer, appType, true);
#else
    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcm(pTpInfo->peer, appType, true);
#endif

    if(!pTcm)
    {
        logError("fails to tpGetTcm for peer(%A).", &pTpInfo->peer);
        goto EXIT;
    }

    if(pTcm->sockfd == -1)
    {
        //check if the peer under quarantine
        if(tpIsInQList(pTpInfo->peer))
        {
            goto EXIT;
        }

        int connStatus;
        if(tpCreateTcp(tpEpFd, &pTpInfo->peer, &pTpInfo->local, &pTcm->sockfd, &connStatus) != OS_STATUS_OK)
        {
            tpReleaseTcm(pTcm);
			tpStatus = TRANSPORT_STATUS_TCP_FAIL;
            goto EXIT;
        }

		if(fd)
		{
			*fd = pTcm->sockfd;
		}

        if(connStatus == 0)
        {
            pTcm->isTcpConnDone = true;
        }
        else
        {
            pTcm->isTcpConnDone = false;
            osListPlus_append(&pTcm->tcpConn.appIdList, appId);
logError("to-remove, TCM2, appId=%p", appId);
        }
//  pTcm->tcpConn.tcpConnTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_TIMEOUT, sipTransport_onTimeout, tcpMapElement);

        mdebug(LM_TRANSPORT, "sockfd(%d) connStatus=%d, pTcm=%p", pTcm->sockfd, connStatus, pTcm);

        if(!pTcm->isTcpConnDone)
        {
            tpStatus = TRANSPORT_STATUS_TCP_CONN;
            goto EXIT;
        }
    }

    if(pTcm->isTcpConnDone)
    {
        tpStatus = TRANSPORT_STATUS_TCP_OK;

		//in case app just wants to check/make TCP connection
		if(!pBuf)
		{
			goto EXIT;
		}

        if(pTpInfo->protocolUpdatePos != 0)
        {
            osMBuf_modifyStr(pBuf, "TCP", 3, pTpInfo->protocolUpdatePos);
        }

        logInfo("send a message via TCP FD=%d, dest addr=%A, sip message=\n%M", pTcm->sockfd, &pTpInfo->peer, pBuf);
        int len = write(pTcm->sockfd, pBuf->buf, pBuf->end);
        if(len == 0 || len == pBuf->end)
        {
            //update the tcp connection's keep alive
            pTcm->msgConnInfo.isUsed = true;
        }
        else if(len == -1)
        {
            logError("write to peer fails, fd=%d, errno=%d.", pTcm->sockfd, errno);
			tpStatus = TRANSPORT_STATUS_TCP_FAIL;
        }
        else if(len != pBuf->end)
        {
            logError("write for tcpfd (%d) returns len (%d), the message len is %d, errno=%d.", pTcm->sockfd, len, pBuf->end, errno);
        	tpStatus = TRANSPORT_STATUS_TCP_FAIL;
        }
    }
    else
    {
        tpTcmAddUser(pTcm, appId);
        tpStatus = TRANSPORT_STATUS_TCP_CONN;
    }

EXIT:
    return tpStatus;
}


int com_getTpFd()
{
	return tpEpFd;
}

//static transportStatus_e TpCreateAndSendUdp(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd)
