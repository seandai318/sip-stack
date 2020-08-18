/* Copyright 2020, 2019, Sean Dai
 */

#define _GNU_SOURCE		//for pipe2()
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
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"
#include "osResourceMgmt.h"

#include "sipConfig.h"
#include "sipMsgFirstLine.h"
#include "sipUriparam.h"
#include "sipTransMgr.h"
#include "sipTransportIntf.h"
#include "sipAppMain.h"
#include "tcm.h"
#include "transportLib.h"
#include "diaIntf.h"
#include "transportUdpMgmt.h"


extern void dnsTest();	//for dns test, temp here, shall be removed after dns testing

static __thread sipTransportClientSetting_t tpSetting;
static __thread struct sockaddr_in defaultLocalAddr;
static __thread int udpFd=-1, tpEpFd=-1;
static __thread tpLocalSendCallback_h tpLocal_appTypeCallbackMap[TRANSPORT_APP_TYPE_COUNT];
static void sipTpClientOnIpcMsg(osIPCMsg_t* pIpcMsg);

static osStatus_e sipTpClientSendTcp(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf, sipTransportStatus_e* pTransport);
//static osStatus_e sipTpCreateTcp(void* appId, transportIpPort_t* peer, transportIpPort_t* local, tpTcm_t* pTcm);
//static void sipTpNotifyTcpConnUser(transportAppType_e appType, osListPlus_t* pList, transportStatus_e msgType, int tcpfd);
static void sipTpClientTimeout(uint64_t timerId, void* ptr);
static osStatus_e tpClientProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len);


osStatus_e sipAppMainInit(int pipefd[2])
{
    osStatus_e status = OS_STATUS_OK;

    //create IPC pipes
    if(pipe2(pipefd, O_NONBLOCK | O_DIRECT) != 0)
    {
        logError("fails to pipe2, errno=%d.", errno);
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

EXIT:
    return status;
}


void tpLocal_appReg(transportAppType_e appType, tpLocalSendCallback_h callback)
{
	if(appType >= TRANSPORT_APP_TYPE_COUNT)
	{
		logError("appType(%d) is larger than TRANSPORT_APP_TYPE_COUNT(%d).", appType, TRANSPORT_APP_TYPE_COUNT);
		return;
	}

	tpLocal_appTypeCallbackMap[appType] = callback;
}
	

	
void* sipAppMainStart(void* pData)
{
	struct epoll_event event, events[SYS_MAX_EPOLL_WAIT_EVENTS];

    sipTransportClientSetting_t tpSetting = *(sipTransportClientSetting_t*)pData;

	debug("threadId = %u.", (unsigned int)pthread_self());

	osTimerInit(tpSetting.timerfd, tpSetting.ownIpcFd[1], SIP_CONFIG_TIMEOUT_MULTIPLE, sipTpClientTimeout);

	notifyTcpConnUser_h notifier[TRANSPORT_APP_TYPE_COUNT] = {};
	notifier[TRANSPORT_APP_TYPE_SIP] = sipTpNotifyTcpConnUser;
	notifier[TRANSPORT_APP_TYPE_DIAMETER] = diaTpNotifyTcpConnUser;
	tcmInit(notifier, TRANSPORT_APP_TYPE_COUNT);

logError("to-remove, call sipTransInit, size=%u", SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE);
    sipTransInit(SIP_CONFIG_TRANSACTION_HASH_BUCKET_SIZE);

	//app startup function
	if(tpSetting.appStartup != NULL)
	{
		tpSetting.appStartup(tpSetting.appStartupData);
	}

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

    memset(&defaultLocalAddr, 0, sizeof(defaultLocalAddr));

	if(tpConvertPLton(&tpSetting.local, false, &defaultLocalAddr) != OS_STATUS_OK)
	{
		logError("fails to tpConvertPLton for udp, IP=%r, port=%d.", &tpSetting.local.ip, tpSetting.local.port);
		goto EXIT;
	}

    if((udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0 ) 
	{ 
		logError("fails to create UDP socket.");
		goto EXIT;
    } 

    // Bind the socket with the client address 
    if(bind(udpFd, (const struct sockaddr *)&defaultLocalAddr, sizeof(defaultLocalAddr)) < 0 ) 
    { 
        logError("udpSocket bind fails, local IP=%r, udpSocketfd=%d, errno=%d.", &tpSetting.local.ip, udpFd, errno); 
		goto EXIT;
    } 

    logInfo("UDP FD=%d is created, local=%A", udpFd, &defaultLocalAddr);

    //to-do: may need to move this function to other module, like masMain(). that requires the synchronization so that when dia starts to do connection, the epoll in tpMain is ready.
	dia_init("/home/ama/project/app/mas/config");

	//test perform dns test, temporary here
	dnsTest();

	//in transport layer, do not do TCP listening for the new connection, they are the responsible of com.  it only does ipc and tcp client connection listening.  whoever creates the tcp connection will add the connection fd to the tpEpFd. For udp, we do listening for udp created via tpLocal_udpSend().  for udp created when system start up using the default local, send only (may add to listning later though)
	ssize_t len;
	size_t bufLen;
	char* buffer;
	int event_count;
//    size_t ipcMsgAddr;
	osIPCMsg_t ipcMsg;
	tpLocalSendCallback_h udpCallback;
	char udpRcv[SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE];
    while (1)
    {
        event_count = epoll_wait(tpEpFd, events, SYS_MAX_EPOLL_WAIT_EVENTS, -1);
        for(int i = 0; i < event_count; i++) {
			if(events[i].data.fd == tpSetting.ownIpcFd[0])
			{
            	while (1)
            	{
                	//len = read(events[i].data.fd, &ipcMsgAddr, sizeof(size_t));
                    len = read(events[i].data.fd, &ipcMsg, sizeof(osIPCMsg_t));

                	//printf("debug, tpClient, fd=%d, received len=%ld, errno=%d.\n", events[i].data.fd, len, errno);
                	if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
					{
                    	//printf("EAGAIN received, n=%d, errno=%d, EWOULDBLOCK=%d, EAGAIN=%d\n", n, errno, EWOULDBLOCK, EAGAIN);
                    	break;
					}

					//sipTpClientOnIpcMsg((osIPCMsg_t*)ipcMsgAddr);
                    sipTpClientOnIpcMsg(&ipcMsg);
				}
			}
			else if((udpCallback = tpUdpMgmtGetUdpCallback(events[i].data.fd)) != NULL)
			{
				//this is a UDP FD
                while(1)
                {
                    struct sockaddr_in peerAddr;
                    int peerAddrLen = sizeof(peerAddr);
                    len = recvfrom(events[i].data.fd, udpRcv, SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *) &peerAddr, &peerAddrLen);
                    if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EBADF))
                    {
                        debug("EAGAIN received for udpfd(%d).", events[i].data.fd);
                        break;
                    }

                    if(len < 10)
                    {
                        debug("received udp message with len(%d) less than minimum packet size for udp (%d).", len, events[i].data.fd);
                        continue;
                    }

                    osMBuf_t* udpBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
                    memcpy(udpBuf->buf, udpRcv, len);

                    debug("to-remove, peer=%A.", &peerAddr);
                    udpBuf->end = len;
					udpCallback(TRANSPORT_STATUS_UDP, events[i].data.fd, udpBuf);
                }
			}
			else
			{
				if(events[i].events & EPOLLOUT)
				{
					if(events[i].events & (EPOLLERR|EPOLLHUP))
					{
						tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
						logInfo("received events(%0x%x), close connection(fd=%d), notify app.", events[i].events, tpEpFd);
						//close(events[i].data.fd);	
                        //tpDeleteTcm(events[i].data.fd);
					}
					else
					{
						debug("fd(%d) is conncted.", events[i].data.fd);

                    	event.events = EPOLLIN | EPOLLRDHUP;
                    	event.data.fd = events[i].data.fd;
                    	epoll_ctl(tpEpFd, EPOLL_CTL_MOD, events[i].data.fd, &event);

						tpTcmUpdateConn(events[i].data.fd, true);
					}
					continue;
				}

				tpTcm_t* pTcm = tpGetConnectedTcm(events[i].data.fd);
				if(!pTcm)
				{
					logError("received a TCP message from tcpfd (%d) that does not have TCM.", events[i].data.fd);
					continue;
				}

				//pSipMsgBuf->pos = the end of processed received bytes 
				//pSipMsgBuf->end = the end of last received bytes
            	while (1) 
				{
					switch(pTcm->appType)
                    {
                        case TRANSPORT_APP_TYPE_SIP:
                			buffer = &pTcm->msgConnInfo.pMsgBuf->buf[pTcm->msgConnInfo.pMsgBuf->end];
                			bufLen = pTcm->msgConnInfo.pMsgBuf->size - pTcm->msgConnInfo.pMsgBuf->end;
							if(bufLen == 0)
							{
                        		logError("something is wrong on message received from tcpFd(%d), the message size exceeds the allowed SIP MESSAGE SIZE.", events[i].data.fd);
								tpTcmBufInit(pTcm, false);
								buffer = pTcm->msgConnInfo.pMsgBuf->buf;
								bufLen = pTcm->msgConnInfo.pMsgBuf->size;
							}
							break;
						default:
							goto EXIT;
					}

                	len = recv(events[i].data.fd, (char *)buffer, bufLen, MSG_DONTWAIT);

                	if(len == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
                	{
                    	debug("EAGAIN received\n");
                    	break;
                	}
					else if(len == 0)
					{
						mdebug(LM_TRANSPORT, "TCP fd(%d) received len=0.", events[i].data.fd);  
						if(events[i].events & EPOLLRDHUP)
						{
							//close the fd
							mdebug(LM_TRANSPORT, "peer closed the TCP connection for tcpfd (%d).", events[i].data.fd);
	                        tpTcmCloseTcpConn(tpEpFd, events[i].data.fd, true);
						}
						break;
					}	

					tpClientProcessSipMsg(pTcm, events[i].data.fd, len);
				}
			}
        }
    }

EXIT:
	logError("a transport thread fails, exiting...");

	if(tpSetting.ownIpcFd[0] >= 0)
	{
        close(tpSetting.ownIpcFd[0]);
	}
	if(udpFd >= 0)
	{
        close(udpFd);
	}

	tpDeleteAllTcm();

	if(tpEpFd >= 0)
	{
        close(tpEpFd);
	}
        
	exit(EXIT_FAILURE);
}


sipTransportStatus_e sipTpClient_send(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf)
{
	osStatus_e status = OS_STATUS_OK;
	sipTransportStatus_e tpStatus = TRANSPORT_STATUS_UDP;

	if(!pTpInfo || !pSipBuf)
	{
		logError("null pointer, pTpInfo=%p, pSipBuf=%p.", pTpInfo, pSipBuf);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	ssize_t len;
	int sockfd;
	if(pTpInfo->tpType == SIP_TRANSPORT_TYPE_TCP && pTpInfo->tcpFd != -1)
	{
		tpStatus = TRANSPORT_STATUS_TCP_OK;

        if(pTpInfo->protocolUpdatePos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "TCP", 3, pTpInfo->protocolUpdatePos);
        }

		//use this fd
        logInfo("send a SIP message via TCP FD=%d, dest=%A, sip message=\n%M", pTpInfo->tcpFd, &pTpInfo->peer, pSipBuf);
		len = write(pTpInfo->tcpFd, pSipBuf->buf, pSipBuf->end);
		if(len == -1 || len != pSipBuf->end)
		{
			logError("fails to send TCP out, fd=%d, len=%ld.", pTpInfo->tcpFd, len);
			status = OS_ERROR_NETWORK_FAILURE;
		}

		goto EXIT;
	}

	if(pTpInfo->tpType == SIP_TRANSPORT_TYPE_UDP)
	{
        tpStatus = SIP_TRANSPORT_TYPE_UDP;
        if(pTpInfo->protocolUpdatePos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "UDP", 3, pTpInfo->protocolUpdatePos);
        }

		logInfo("send a SIP message via UDP FD=%d, dest=%A, sip message=\n%M", udpFd, &pTpInfo->peer, pSipBuf);
        len = sendto(udpFd, pSipBuf->buf, pSipBuf->end, 0, (const struct sockaddr*) &pTpInfo->peer, sizeof(struct sockaddr_in));
        if(len != pSipBuf->end || len == -1)
        {
            logError("fails to sendto() for udpFd=%d, len=%d, errno=%d.", udpFd, len, errno);
            status = OS_ERROR_NETWORK_FAILURE;
        }
	}
	else
	{
		status = sipTpClientSendTcp(pTrId, pTpInfo, pSipBuf, &tpStatus);
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		tpStatus = TRANSPORT_STATUS_FAIL;
	}

	return tpStatus;
}


//two ways, the same socket, send and receive
transportStatus_e tpLocal_udpSend(transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* pFd)
{
	DEBUG_BEGIN
	transportStatus_e tpStatus = TRANSPORT_STATUS_FAIL;
	if(!pTpInfo || !pBuf)
	{
		logError("null pointer, pTpInfo=%p, pBuf=%p.", pTpInfo, pBuf);
		goto EXIT;
	}

	tpLocalSendCallback_h callback = tpLocal_appTypeCallbackMap[appType];
	{
		if(callback == NULL)
		{	
			logError("callback=NULL for appType=%d.", appType);
			goto EXIT;
		}
	}

	struct sockaddr_in localAddr;
	if(pTpInfo->local.sin_addr.s_addr == 0)
	{
		localAddr.sin_family = defaultLocalAddr.sin_family;
		localAddr.sin_addr.s_addr = defaultLocalAddr.sin_addr.s_addr;
	}

	if(pTpInfo->udpInfo.isEphemeralPort)
	{
		localAddr.sin_port = 0;
	}
	else
	{
		localAddr.sin_port = pTpInfo->local.sin_port;
	}

	int fd = tpUdpMgmtGetFd(appType, localAddr);
	if(fd == -1)
	{
		//does not find a fd, need to create a new udp socket
    	if((fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0 )
    	{
        	logError("fails to create UDP socket.");
        	goto EXIT;
    	}

    	int opt = 1;
    	//has to set SO_REUSEADDR, otherwise, bind() will get EADDRINUSE(98) error when port is specified
    	if(setsockopt(fd,SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) != 0)
    	{
        	logError("fails to setsockopt for SO_REUSEADDR.");
			close(fd);
        	goto EXIT;
    	}

    	if(setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int)) != 0)
    	{
        	logError("fails to setsockopt for SO_REUSEPORT.");
			close(fd);
        	goto EXIT;
    	}

    	// Bind the socket with the local address
    	if(bind(fd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0)
    	{
        	logError("udpSocket bind fails, local addr=%A, udpSocketfd=%d, errno=%d.", &localAddr, fd, errno);
			close(fd);
        	goto EXIT;
    	}

		//always listening even if pTpInfo->udpInfo.isUdpWaitResponse = true
    	struct epoll_event event;
		event.events = EPOLLIN;
    	event.data.fd = fd;
    	if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, fd, &event))
    	{
        	logError("fails to add file descriptor (%d) to epoll(%d), errno=%d.\n", fd, tpEpFd, errno);
			close(fd);
        	goto EXIT;
    	}

    	debug("udp listening fd =%d (%A) is added into epoll fd(%d).", fd, &localAddr, tpEpFd);

		//set new fd in the udpmgmt
		tpUdpMgmtSetFd(appType, localAddr, callback, fd);
	}

    logInfo("send a UDP message via UDP FD=%d, dest=%A", fd, &pTpInfo->peer);
    ssize_t len = sendto(fd, pBuf->buf, pBuf->end, 0, (const struct sockaddr*) &pTpInfo->peer, sizeof(struct sockaddr_in));
    if(len != pBuf->end || len == -1)
    {
		logError("fails to sendto() for udpFd=%d, len=%d, errno=%d.", fd, len, errno);
    }
    else
    {
		if(pFd)
		{
			*pFd = fd;
		}
        tpStatus = TRANSPORT_STATUS_UDP;
    }

	logError("tpUdpMgmtGetUdpCallback(fd=%d)=%p", fd, tpUdpMgmtGetUdpCallback(fd));
EXIT:
	DEBUG_END
	return tpStatus;
}



static void sipTpClientOnIpcMsg(osIPCMsg_t* pIpcMsg)
{
	switch (pIpcMsg->interface)
	{
		case OS_TIMER_ALL:
		case OS_TIMER_TICK:
			osTimerGetMsg(pIpcMsg->interface, pIpcMsg->pMsg);
			break;
		case OS_SIP_TRANSPORT_SERVER:
			sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, pIpcMsg->pMsg, 0);
			break;
		case OS_DIA_TRANSPORT:
			diaMgr_onMsg(pIpcMsg->pMsg);
			break;
		default:
			logError("received ipc message from unknown interface (%d).", pIpcMsg->interface);
			break;
	}
}


//send a packet when transaction does not specify the sending fd	
static osStatus_e sipTpClientSendTcp(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf, sipTransportStatus_e* pTransport)
{
	osStatus_e status = OS_STATUS_OK;
    int locValue;

	*pTransport = TRANSPORT_STATUS_TCP_OK;

#if 0   //use network address	
    struct sockaddr_in peer = {};
//    struct sockaddr_in local = {};

#if 0
	//do not bind port, use ephemeral port.
	if(tpConvertPLton(&pTpInfo->local, false, &local) != OS_STATUS_OK)
	{
        logError("fails to tpConvertPLton for local IP=%r, errno=%d.", pTpInfo->local.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
#endif
    if(tpConvertPLton(&pTpInfo->peer, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to tpConvertPLton for peer IP=%r, errno=%d.", pTpInfo->peer.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//first check if there is already a TCP connection exists
	tpTcm_t* pTcm = tpGetTcm(peer, TRANSPORT_APP_TYPE_SIP, true);
#else
    //first check if there is already a TCP connection exists
    tpTcm_t* pTcm = tpGetTcm(pTpInfo->peer, TRANSPORT_APP_TYPE_SIP, true);
#endif
	if(!pTcm)
	{
		logError("fails to tpGetTcm for peer(%A).", &pTpInfo->peer);
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	if(pTcm->sockfd == -1)
	{
		//check if the peer under quarantine
		if(tpIsInQList(pTpInfo->peer))
		{
			*pTransport = TRANSPORT_STATUS_TCP_FAIL;
			goto EXIT;
		}

		int connStatus;
		status = tpCreateTcp(tpEpFd, &pTpInfo->peer, &pTpInfo->local, &pTcm->sockfd, &connStatus);
		if(status != OS_STATUS_OK)
		{
            tpReleaseTcm(pTcm);
			goto EXIT;
		}

    	if(connStatus == 0)
    	{
        	pTcm->isTcpConnDone = true;
    	}
    	else
    	{
        	pTcm->isTcpConnDone = false;
        	osListPlus_append(&pTcm->tcpConn.appIdList, pTrId);
logError("to-remove, TCM2, appId=%p", pTrId);
    	}
//  pTcm->tcpConn.tcpConnTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_TIMEOUT, sipTransport_onTimeout, tcpMapElement);

    	mdebug(LM_TRANSPORT, "sockfd(%d) connStatus=%d, pTcm=%p", pTcm->sockfd, connStatus, pTcm);

		if(!pTcm->isTcpConnDone)
		{
			*pTransport = TRANSPORT_STATUS_TCP_CONN; 
			goto EXIT;
		}
	}

	if(pTcm->isTcpConnDone)
	{
		*pTransport = TRANSPORT_STATUS_TCP_OK;

        if(pTpInfo->protocolUpdatePos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "TCP", 3, pTpInfo->protocolUpdatePos);
        }

        logInfo("send a SIP message via TCP FD=%d, dest=%A, sip message=\n%M", pTcm->sockfd, &pTpInfo->peer, pSipBuf);
        int len = write(pTcm->sockfd, pSipBuf->buf, pSipBuf->end);
        if(len == 0 || len == pSipBuf->end)
        {
			//update the tcp connection's keep alive
            pTcm->msgConnInfo.isUsed = true;
        }
        else if(len == -1)
        {
			logError("write to peer fails, fd=%d, errno=%d.", pTcm->sockfd, errno);
            status = OS_ERROR_NETWORK_FAILURE;
        }
        else if(len != pSipBuf->end)
        {
            logError("write for tcpfd (%d) returns len (%d), the message len is %d, errno=%d.", pTcm->sockfd, len, pSipBuf->end, errno);
            status = OS_ERROR_NETWORK_FAILURE;
		}
	}
	else
	{
		tpTcmAddUser(pTcm, pTrId);
		*pTransport = TRANSPORT_STATUS_TCP_CONN;
	}

EXIT:
	return status;
}

#if 0
static osStatus_e sipTpCreateTcp(void* appId, transportIpPort_t* peer, transportIpPort_t* local, tpTcm_t* pTcm)
{
	osStatus_e status = OS_STATUS_OK;
	int sockfd;

    if((sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        logError("could not open a TCP socket, errno=%d.", errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    struct sockaddr_in src;
    status = tpConvertPLton(local, false, &src);
	if(status != OS_STATUS_OK)
    {
        logError("fails to perform tpConvertPLton for local.");
		goto EXIT;
    }

    status = tpConvertPLton(peer, true, &pTcm->peer);
    if(status != OS_STATUS_OK)
    {
        logError("fails to perform tpConvertPLton for peer.");
        goto EXIT;
    }


    if(bind(sockfd, (const struct sockaddr *)&src, sizeof(src)) < 0 )
    {
		logError("fails to bind for sockfd (%d), localIP=%r, localPort=%d, errno=%d", sockfd, &local->ip, local->port, errno);
		close(sockfd);
		status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLRDHUP;		//after connection is established, events will be changed to EPOLLIN
    event.data.fd = sockfd;
    epoll_ctl(tpEpFd, EPOLL_CTL_ADD, sockfd, &event);

	int connStatus = connect(sockfd, (struct sockaddr*)&pTcm->peer, sizeof(pTcm->peer));
    if(connStatus != 0 && errno != EINPROGRESS) 
	{
		logError("fails to connect() for peer ip=%r, port=%d.", peer->ip, peer->port);
		status = OS_ERROR_NETWORK_FAILURE;
		goto EXIT;
    }

    pTcm->isUsing = true;
    pTcm->sockfd = sockfd;
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
//	pTcm->tcpConn.tcpConnTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_TIMEOUT, sipTransport_onTimeout, tcpMapElement);

	mdebug(LM_TRANSPORT, "sockfd(%d) connStatus=%d, pTcm=%p", sockfd, connStatus, pTcm);	
EXIT:
	return status;
}
#endif

#if 0
//if msgType != SIP_TRANS_MSG_TYPE_TX_TCP_READY, set tcpfd=-1
static void sipTpNotifyTcpConnUser(transportAppType_e appType, osListPlus_t* pList, transportStatus_e tpMsgType, int tcpfd)
{
	switch (appType)
	{
		case TRANSPORT_APP_TYPE_SIP:
		{
			sipTransportStatusMsg_t sipTpMsg;
			sipTransMsgType_e msgType;

			if(tpMsgType = TRANSPORT_STATUS_TCP_OK)
			{
				msgType = SIP_TRANS_MSG_TYPE_TX_TCP_READY;
				sipTpMsg.tcpFd = tcpfd;
			}
			else
			{
				msgType = SIP_TRANS_MSG_TYPE_TX_FAILED;
                sipTpMsg.tcpFd = -1;
			}

			if(pList->first)
			{
				sipTpMsg.pTransId = pList->first;
				sipTrans_onMsg(msgType, &sipTpMsg, 0);
			}

			if(pList->num > 1)
			{
				osListElement_t* pLE = pList->more.head;
				while(pLE)
				{
					sipTpMsg.pTransId = pLE->data;
	        		sipTrans_onMsg(msgType, &sipTpMsg, 0);
					pLE = pLE->next;
				}
			}
			break;
		}
		default:
			logInfo("appType(%d) is unhandled now.", appType);
			break;
	}

    //clear appIdList
	osListPlus_clear(pList);
}
#endif

static void sipTpClientTimeout(uint64_t timerId, void* ptr)
{
	logInfo("received timeout for timerId=%d.", timerId);
}


static osStatus_e tpClientProcessSipMsg(tpTcm_t* pTcm, int tcpFd, ssize_t len)
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
    ssize_t remaining = sipTpAnalyseMsg(pTcm->msgConnInfo.pMsgBuf, &pTcm->msgConnInfo.sipState, len, &nextStart);
    if(remaining < 0)
    {
        mdebug(LM_TRANSPORT, "remaining=%d, less than 0.", remaining);
        goto EXIT;
    }
    else
    {
        osMBuf_t* pCurSipBuf = pTcm->msgConnInfo.pMsgBuf;
        bool isBadMsg = pTcm->msgConnInfo.sipState.isBadMsg;
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
			char ip[INET_ADDRSTRLEN]={};
			inet_ntop(AF_INET, &pTcm->peer.sin_addr, ip, INET_ADDRSTRLEN);
			mlogInfo(LM_TRANSPORT, "received a sip Msg from fd(%d) %s:%d, the msg=\n%M", pTcm->sockfd, ip, ntohs(pTcm->peer.sin_port), pCurSipBuf);

			pTcm->msgConnInfo.isUsed = true;

			sipTransportMsgBuf_t sipTpMsg;
			sipTpMsg.pSipBuf = pCurSipBuf;
			sipTpMsg.tcpFd = -1;
			sipTpMsg.isCom = false;
			//sipTpMsg.tpId = NULL;
			sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, &sipTpMsg, 0);
        }
        else
        {
            mdebug(LM_TRANSPORT, "received a bad msg, drop.");
        }
    }

EXIT:
    return status;
}


int appMain_getTpFd()
{
    return tpEpFd;
}

