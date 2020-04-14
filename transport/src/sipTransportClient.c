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
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"
#include "osResourceMgmt.h"

#include "sipConfig.h"
#include "sipMsgFirstLine.h"
#include "sipUriparam.h"
#include "sipTransMgr.h"
#include "sipTransportIntf.h"
#include "sipTransportClient.h"
#include "sipTcm.h"
#include "sipTransportLib.h"



static __thread sipTransportClientSetting_t tpSetting;
static __thread int udpFd=-1, tpEpFd=-1;

static void sipTpClientOnIpcMsg(osIPCMsg_t* pIpcMsg);
static osStatus_e sipTpClientSendTcp(void* pTrId, sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf, sipTransportStatus_e* pTransport);
static osStatus_e sipTpCreateTcp(void* appId, sipTransportIpPort_t* peer, sipTransportIpPort_t* local, sipTpTcm_t* pTcm);
static void sipTpNotifyTcpConnUser(osListPlus_t* pList, sipTransMsgType_e msgType, int tcpfd);
static void sipTpClientTimeout(uint64_t timerId, void* ptr);


osStatus_e sipTransportClientInit(int pipefd[2])
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


void* sipTransportClientStart(void* pData)
{
	struct sockaddr_in localAddr;
	struct epoll_event event, events[SYS_MAX_EPOLL_WAIT_EVENTS];

    sipTransportClientSetting_t tpSetting = *(sipTransportClientSetting_t*)pData;

	debug("threadId = %u.", (unsigned int)pthread_self());

	osTimerInit(tpSetting.timerfd, tpSetting.ownIpcFd[1], SIP_CONFIG_TIMEOUT_MULTIPLE, sipTpClientTimeout);

	sipTcmInit(sipTpNotifyTcpConnUser);

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

    memset(&localAddr, 0, sizeof(localAddr));
//    memset(&peerAddr, 0, sizeof(peerAddr));

	if(sipTpConvertPLton(&tpSetting.local, false, &localAddr) != OS_STATUS_OK)
	{
		logError("fails to sipTpConvertPLton for udp, IP=%r, port=%d.", &tpSetting.local.ip, tpSetting.local.port);
    // Filling server information
//    localAddr.sin_family    = AF_INET; // IPv4
//    localAddr.sin_addr.s_addr = tpSetting.local.ip;
//    localAddr.sin_port = htons(tpSetting.local.port);
		goto EXIT;
	}

    if((udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0 ) 
	{ 
		logError("fails to create UDP socket.");
		goto EXIT;
    } 

    // Bind the socket with the server address 
    if(bind(udpFd, (const struct sockaddr *)&localAddr, sizeof(localAddr)) < 0 ) 
    { 
        logError("udpSocket bind fails, local IP=%r, udpSocketfd=%d, errno=%d.", &tpSetting.local.ip, udpFd, errno); 
		goto EXIT;
    } 

    logInfo("UDP FD=%d is created, local ip=%s, port=%d", udpFd, inet_ntoa(localAddr.sin_addr), ntohs(localAddr.sin_port));

	//in transport layer, do not do UDP listening, neither we do TCP listening for the new connection, they are the responsible of com.  it only does ipc and tcp client connection listening.  whoever creates the tcp connection will add the connection fd to the tpEpFd. 
	ssize_t len;
	size_t bufLen;
	char* buffer;
	int event_count;
//    size_t ipcMsgAddr;
	osIPCMsg_t ipcMsg;
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
			else
			{
				if(events[i].events & EPOLLOUT)
				{
					if(events[i].events & EPOLLERR)
					{
						close(events[i].data.fd);	
                        sipTpDeleteTcm(events[i].data.fd);
					}
					else
					{
						debug("fd(%d) is conncted.", events[i].data.fd);

                    	event.events = EPOLLIN | EPOLLRDHUP;
                    	event.data.fd = events[i].data.fd;
                    	epoll_ctl(tpEpFd, EPOLL_CTL_MOD, events[i].data.fd, &event);

						sipTpTcmUpdateConn(events[i].data.fd, true);
					}
					continue;
				}

				sipTpTcm_t* pTcm = sipTpGetConnectedTcm(events[i].data.fd);
				if(!pTcm)
				{
					logError("received a TCP message from tcpfd (%d) that does not have TCM.", events[i].data.fd);
					continue;
				}

				//pSipMsgBuf->pos = the end of processed received bytes 
				//pSipMsgBuf->end = the end of last received bytes
            	while (1) 
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
							close(events[i].data.fd);
							sipTpDeleteTcm(events[i].data.fd);
						}
						break;
					}	

					//basic sanity check to remove leading \r\n.  here we also ignore other invalid chars, 3261 only says \r\n
					if(pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->pos == 0 && (pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0] < 'A' || pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0] > 'Z'))
					{
                        mdebug(LM_TRANSPORT, "received pkg proceeded with invalid chars, char[0]=0x%x, len=%d.", pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf[0],len); 
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
						if(!sipTpTcmBufInit(&pTcm->tcpKeepAlive.sipBuf, isBadMsg ? false : true))
						{
							logError("fails to init a TCM pSipMsgBuf.");
							goto EXIT;
						}

						if(remaining > 0)
						{
							memcpy(pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->buf, &pCurSipBuf->buf[nextStart], remaining);
						}
						pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->end = remaining;
						pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf->pos = 0;

						if(!isBadMsg)
						{
    						char ip[INET_ADDRSTRLEN]={};
    						inet_ntop(AF_INET, &pTcm->peer.sin_addr, ip, INET_ADDRSTRLEN);
							mlogInfo(LM_TRANSPORT, "received a sip Msg from fd(%d) %s:%d, the msg=\n%M", pTcm->sockfd, ip, ntohs(pTcm->peer.sin_port), pCurSipBuf);

							pTcm->tcpKeepAlive.isUsed = true;

							sipTransportMsgBuf_t sipTpMsg;
							sipTpMsg.pSipBuf = pCurSipBuf;
							sipTpMsg.tcpFd = -1;
							sipTpMsg.isServer = false;
							//sipTpMsg.tpId = NULL;					
							sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, &sipTpMsg, 0);
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

	if(tpSetting.ownIpcFd[0] >= 0)
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


sipTransportStatus_e sipTpClient_send(void* pTrId, sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf)
{
	osStatus_e status = OS_STATUS_OK;
	sipTransportStatus_e tpStatus = SIP_TRANSPORT_STATUS_UDP;

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
		tpStatus = SIP_TRANSPORT_STATUS_TCP_OK;

        if(pTpInfo->viaProtocolPos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "TCP", 3, pTpInfo->viaProtocolPos);
        }

		//use this fd
        logInfo("send a SIP message via TCP FD=%d, dest ip=%r, port=%d, sip message=\n%M", pTpInfo->tcpFd, &pTpInfo->peer.ip, pTpInfo->peer.port, pSipBuf);
		len = write(pTpInfo->tcpFd, pSipBuf->buf, pSipBuf->end);
		if(len == -1 || len != pSipBuf->end)
		{
			logError("fails to send TCP out, fd=%d, len=%ld.", pTpInfo->tcpFd, len);
			status = OS_ERROR_NETWORK_FAILURE;
		}

		goto EXIT;
	}

	
#if 0		//done in sipTransportMgr
    //check which transport protocol to use
    bool isUDP = true;
    switch(sipConfig_getTransport(&pTpInfo->peer.ip, pTpInfo->peer.port))
    {
        case SIP_TRANSPORT_TYPE_TCP:
            isUDP = false;
            break;
        case SIP_TRANSPORT_TYPE_ANY:
            if(pSipBuf->end > OS_TRANSPORT_MAX_MTU - OS_SIP_TRANSPORT_BUFFER_SIZE)
            {
                isUDP = false;
            }
            break;
        default:
            break;
    }
#endif

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

		logInfo("send a SIP message via UDP FD=%d, dest ip=%r, port=%d, sip message=\n%M", udpFd, &pTpInfo->peer.ip, pTpInfo->peer.port, pSipBuf);
        len = sendto(udpFd, pSipBuf->buf, pSipBuf->end, 0, (const struct sockaddr*) &dest, sizeof(dest));
//        len = sendto(udpFd, "hello", 5, 0, (const struct sockaddr*) &dest, sizeof(dest));
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
		tpStatus = SIP_TRANSPORT_STATUS_FAIL;
	}

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
		default:
			logError("received ipc message from unknown interface (%d).", pIpcMsg->interface);
			break;
	}
}


//send a packet when transaction does not specify the sending fd	
static osStatus_e sipTpClientSendTcp(void* pTrId, sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf, sipTransportStatus_e* pTransport)
{
	osStatus_e status = OS_STATUS_OK;
    int locValue;

	*pTransport = SIP_TRANSPORT_STATUS_TCP_OK;
	
    struct sockaddr_in peer = {};
    struct sockaddr_in local = {};

	//do not bind port, use ephemeral port.
	if(sipTpConvertPLton(&pTpInfo->local, false, &local) != OS_STATUS_OK)
	{
        logError("fails to sipTpConvertPLton for local IP=%r, errno=%d.", pTpInfo->local.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(sipTpConvertPLton(&pTpInfo->peer, true, &peer) != OS_STATUS_OK)
    {
        logError("fails to sipTpConvertPLton for peer IP=%r, errno=%d.", pTpInfo->peer.ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//first check if there is already a TCP connection exists
	sipTpTcm_t* pTcm = sipTpGetTcm(peer, true);
	if(!pTcm)
	{
		logError("fails to sipTpGetTcm for ip=%s, port=%d.", pTpInfo->peer.ip, pTpInfo->peer.port);
		status = OS_ERROR_SYSTEM_FAILURE;
		goto EXIT;
	}

	if(!pTcm->isUsing)
	{
		//check if the peer under quarantine
		if(sipTpIsInQList(peer))
		{
			*pTransport = SIP_TRANSPORT_STATUS_TCP_FAIL;
			goto EXIT;
		}

		status = sipTpCreateTcp(pTrId, &pTpInfo->peer, &pTpInfo->local, pTcm);
		if(status != OS_STATUS_OK)
		{
			goto EXIT;
		}

		if(!pTcm->isTcpConnDone)
		{
			*pTransport = SIP_TRANSPORT_STATUS_TCP_CONN; 
			goto EXIT;
		}
	}

	if(pTcm->isTcpConnDone)
	{
		*pTransport = SIP_TRANSPORT_STATUS_TCP_OK;

        if(pTpInfo->viaProtocolPos != 0)
        {
            osMBuf_modifyStr(pSipBuf, "TCP", 3, pTpInfo->viaProtocolPos);
        }

        logInfo("send a SIP message via TCP FD=%d, dest ip=%r, port=%d, sip message=\n%M", pTcm->sockfd, &pTpInfo->peer.ip, pTpInfo->peer.port, pSipBuf);
        int len = write(pTcm->sockfd, pSipBuf->buf, pSipBuf->end);
        if(len == 0 || len == pSipBuf->end)
        {
			//update the tcp connection's keep alive
            pTcm->tcpKeepAlive.isUsed = true;
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
		sipTpTcmAddUser(pTcm, pTrId);
		*pTransport = SIP_TRANSPORT_STATUS_TCP_CONN;
	}

EXIT:
	return status;
}


static osStatus_e sipTpCreateTcp(void* appId, sipTransportIpPort_t* peer, sipTransportIpPort_t* local, sipTpTcm_t* pTcm)
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
    status = sipTpConvertPLton(local, false, &src);
	if(status != OS_STATUS_OK)
    {
        logError("fails to perform sipTpConvertPLton for local.");
		goto EXIT;
    }

    status = sipTpConvertPLton(peer, true, &pTcm->peer);
    if(status != OS_STATUS_OK)
    {
        logError("fails to perform sipTpConvertPLton for peer.");
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
		osListPlus_append(&pTcm->tcpConn.sipTrIdList, appId);
logError("to-remove, TCM2, appId=%p", appId);
	}
//	pTcm->tcpConn.tcpConnTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_TIMEOUT, sipTransport_onTimeout, tcpMapElement);

	mdebug(LM_TRANSPORT, "sockfd(%d) connStatus=%d, pTcm=%p", sockfd, connStatus, pTcm);	
EXIT:
	return status;
}


//if msgType != SIP_TRANS_MSG_TYPE_TX_TCP_READY, set tcpfd=-1
static void sipTpNotifyTcpConnUser(osListPlus_t* pList, sipTransMsgType_e msgType, int tcpfd)
{
	sipTransportStatusMsg_t sipTpMsg;
//    sipTransMsg_t sipTrMsg;

	if(msgType == SIP_TRANS_MSG_TYPE_TX_TCP_READY)
	{
//    	sipTrMsg.sipMsgType = msgType;
    	sipTpMsg.tcpFd = tcpfd;
	}
	else
	{
		sipTpMsg.tcpFd = -1;
	}

logError("to-remove, TCM3, pList->num=%d", pList->num);
	if(pList->first)
	{
		sipTpMsg.pTransId = pList->first;
logError("to-remove, TCM1, pTransId=%p", sipTpMsg.pTransId);
		sipTrans_onMsg(msgType, &sipTpMsg, 0);
	}

	if(pList->num > 1)
	{
		osListElement_t* pLE = pList->more.head;
		while(pLE)
		{
			sipTpMsg.pTransId = pLE->data;
logError("to-remove, TCM2, pTransId=%p", sipTpMsg.pTransId);
	        sipTrans_onMsg(msgType, &sipTpMsg, 0);
			pLE = pLE->next;
		}
	}

    //clear sipTrIdList
	osListPlus_clear(pList);
}


static void sipTpClientTimeout(uint64_t timerId, void* ptr)
{
	logInfo("received timeout for timerId=%d.", timerId);
}
