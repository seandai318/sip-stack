#include <unistd.h>
#include <sys/socket.h>
//#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//#include <netdb.h>
//#include <stdio.h>
//#include <string.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "osDebug.h"
#include "osMBuf.h"
#include "osTimer.h"
//#include "osConfig.h"
//#include "osResourceMgmt.h"

#include "sipConfig.h"
//#include "sipMsgFirstLine.h"
//#include "sipUriparam.h"
//#include "sipTransMgr.h"
//#include "sipTransportMgr.h"
#include "sipTransportIntf.h"
#include "sipTcm.h"


static void sipTpOnKATimeout(uint64_t timerId, void* ptr);
static void sipTpOnQTimeout(uint64_t timerId, void* ptr);


//this is shared by both server and client/transport threads
static sipTpTcm_t sipTCM[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static uint8_t sipTcmMaxNum = 0;

//in TpServer.c, pTcm->tcpKeepAlive is modified and is not Rwlock protected, it is assumed nobody messes with this field except TpServer.c, the same for TpClient.c
static pthread_rwlock_t tcmRwlock;
static __thread sipTpQuarantine_t sipTpQList[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static __thread uint8_t sipTpMaxQNum = 0;
static __thread notifyTcpConnUser_h notifyTcpConnUser = NULL;


void sipTcmInit(notifyTcpConnUser_h notifier)
{
	notifyTcpConnUser = notifier;

	pthread_rwlock_init(&tcmRwlock,NULL);
}


//isReserveOne=true will reserve a tcm if a matching is not found
sipTpTcm_t* sipTpGetTcm(struct sockaddr_in peer, bool isReseveOne)
{
	sipTpTcm_t* pTcm = NULL;

#if 0
    struct sockaddr_in peer = {};

    peer.sin_port = htons(peerPort);
    if (inet_pton(AF_INET, peerIp, &peer.sin_addr.s_addr) == 0)
    {
        logError("inet_pton for peerIp=%s, errno=%d.", peerIp, errno);
        goto EXIT;
    }
#endif

	if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
	{
		logError("fails to acquire rdlock for tcmRwlock.");
		return NULL;
	}

	bool isMatch = false;
	for(int i=0; i<sipTcmMaxNum; i++)
	{
		if(!sipTCM[i].isUsing)
		{
			if(isReseveOne && !pTcm)
			{
				pTcm = &sipTCM[i];
			}
			continue;
		}

		if((sipTCM[i].peer.sin_port == peer.sin_port) && (sipTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr))
		{
			pTcm = &sipTCM[i];
			goto EXIT;
		}
	}

	if(!pTcm)
	{
		if(sipTcmMaxNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
		{
			pTcm = &sipTCM[sipTcmMaxNum++];
		}
	}

EXIT:
	pthread_rwlock_unlock(&tcmRwlock);
	return pTcm;
}


sipTpTcm_t* sipTpGetTcmByFd(int tcpFd, struct sockaddr_in peer)
{
    sipTpTcm_t* pTcm = NULL;

#if 0
    struct sockaddr_in peer = {};

    peer.sin_port = htons(peerPort);
    if (inet_pton(AF_INET, peerIp, &peer.sin_addr.s_addr) == 0)
    {
        logError("inet_pton for peerIp=%s, errno=%d.", peerIp, errno);
        goto EXIT;
    }
#endif
    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return NULL;
    }

    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(sipTCM[i].isUsing)
        {
			if(sipTCM[i].sockfd == tcpFd)
			{ 
        		if((sipTCM[i].peer.sin_port == peer.sin_port) && (sipTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr))
        		{
            		pTcm = &sipTCM[i];
            		goto EXIT;
        		}
    		}
		}
	}

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return pTcm;
}


sipTpTcm_t* sipTpGetConnectedTcm(int tcpFd)
{
    sipTpTcm_t* pTcm = NULL;

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return NULL;
    }

    bool isMatch = false;
    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(!sipTCM[i].isUsing)
        {
			continue;
		}

        if(sipTCM[i].sockfd = tcpFd)
        {
            pTcm = &sipTCM[i];
			if(!pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf)
			{
				pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
				if(!pTcm->tcpKeepAlive.sipBuf.pSipMsgBuf)
				{
					logError("fails to allocate memory for pTcm->tcpKeepAlive.sipBuf->pSipMsgBuf.");
					pTcm = NULL;
				}
			}
            goto EXIT;
        }
    }

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return pTcm;
}	


osStatus_e sipTpDeleteTcm(int tcpfd)
{
    osStatus_e status = OS_STATUS_OK;

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return OS_ERROR_SYSTEM_FAILURE;
    }

    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(!sipTCM[i].isUsing)
        {
            continue;
        }

        if(sipTCM[i].sockfd = tcpfd)
        {
			sipTCM[i].isUsing = false;
			logError("to-remove, tcm, sipTCM[%d].isUsing = false", i);
			if(sipTCM[i].isTcpConnDone)
			{
				osStopTimer(sipTCM[i].tcpKeepAlive.keepAliveTimerId);
				sipTCM[i].tcpKeepAlive.keepAliveTimerId = 0;
				if(sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf)
				{
					osMBuf_dealloc(sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf);
					sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf = NULL;
				}

				sipTCM[i].isTcpConnDone = false;
			}
			else
			{
				//do nothing, to-do, shall we notify tcp conn user?
			}

			memset(&sipTCM[i], 0, sizeof(sipTpTcm_t));
			sipTCM[i].sockfd = -1;

            goto EXIT;
        }
    }

    logError("fails to find a matching tcm, tcpfd = %d.", tcpfd);
    status = OS_ERROR_INVALID_VALUE;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
    return status;
}


void sipTpDeleteAllTcm()
{
    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return;
    }

    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(!sipTCM[i].isUsing)
        {
            continue;
        }

        sipTCM[i].isUsing = false;
            logError("to-remove, tcm, sipTCM[%d].isUsing = false", i);

        if(sipTCM[i].isTcpConnDone)
        {
            osStopTimer(sipTCM[i].tcpKeepAlive.keepAliveTimerId);
            sipTCM[i].tcpKeepAlive.keepAliveTimerId = 0;
            if(sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf)
            {
                osMBuf_dealloc(sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf);
                sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf = NULL;
        	}  

            sipTCM[i].isTcpConnDone = false;
		}
        else
        {
            //do nothing, to-do, shall we notify tcp conn user?
        }

		close(sipTCM[i].sockfd);

        memset(&sipTCM[i], 0, sizeof(sipTpTcm_t));
        sipTCM[i].sockfd = -1;
    }

    pthread_rwlock_unlock(&tcmRwlock);
}


osStatus_e sipTpTcmAddUser(sipTpTcm_t* pTcm, void* pTrId)
{
    osStatus_e status = OS_STATUS_OK;

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return OS_ERROR_SYSTEM_FAILURE;
    }

	if(!pTcm->isUsing)
	{
		logError("try to add a user to a not using TCM.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pTcm->isTcpConnDone)
	{
        logError("try to add a user to a TCM that has established TCP connection.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	osListPlus_append(&pTcm->tcpConn.sipTrIdList, pTrId);

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return status;
}


osStatus_e sipTpTcmAddFd(int tcpfd, struct sockaddr_in* peer)
{
    osStatus_e status = OS_STATUS_OK;
	sipTpTcm_t* pTcm = NULL;

	if(!peer)
	{
		logError("null pointer, peer.");
		return OS_ERROR_NULL_POINTER;
	}

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return OS_ERROR_SYSTEM_FAILURE;
    }

    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(!sipTCM[i].isUsing)
        {
			pTcm = &sipTCM[i];
			break;
		}
	}

    if(!pTcm)
    {
        if(sipTcmMaxNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
        {
            pTcm = &sipTCM[sipTcmMaxNum++];
        }
    }

	if(pTcm)
	{
		pTcm->isUsing = true;
		pTcm->isTcpConnDone = true;
logError("to-remove, PTCM, pTcm=%p set to true", pTcm);
		pTcm->sockfd = tcpfd;
		pTcm->peer = *peer;
		pTcm->tcpKeepAlive.isUsed = false;
//		sipTpTcmBufInit(&pTcm->tcpKeepAlive.sipBuf, false);
        sipTpTcmBufInit(&pTcm->tcpKeepAlive.sipBuf, true);
		pTcm->tcpKeepAlive.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, pTcm);

		mdebug(LM_TRANSPORT, "tcpfd(%d, %s:%d) is added to pTcm(%p)", tcpfd, inet_ntoa(peer->sin_addr), ntohs(peer->sin_port), pTcm);  
		goto EXIT;
	}

	status = OS_ERROR_INVALID_VALUE;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	sipTpListUsedTcm();
	return status;
}


//update TCM conn status for a waiting for conn TCM
osStatus_e sipTpTcmUpdateConn(int tcpfd, bool isConnEstablished)
{
	osStatus_e status = OS_STATUS_OK;

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return OS_ERROR_SYSTEM_FAILURE;
    }

    for(int i=0; i<sipTcmMaxNum; i++)
    {
        if(!sipTCM[i].isUsing)
        {
			continue;
        }

        if(sipTCM[i].sockfd == tcpfd)
        {
			if(sipTCM[i].isTcpConnDone)
			{
				logError("try to update a TCP connection status for fd (%d), but tcm(%p) shows isTcpConnDone = true.", tcpfd, &sipTCM[i]);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			if(isConnEstablished)
			{
				sipTCM[i].isTcpConnDone = true;

				if(notifyTcpConnUser)
				{
					notifyTcpConnUser(&sipTCM[i].tcpConn.sipTrIdList, SIP_TRANS_MSG_TYPE_TX_TCP_READY, tcpfd);
				}
				sipTCM[i].tcpKeepAlive.sipBuf.pSipMsgBuf = osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE);
				sipTCM[i].tcpKeepAlive.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, &sipTCM[i]);
			}
			else
			{
				if(notifyTcpConnUser)
				{
                	notifyTcpConnUser(&sipTCM[i].tcpConn.sipTrIdList, SIP_TRANS_MSG_TYPE_TX_FAILED, 0);
				}

            	sipTCM[i].isUsing = false;
logError("to-remove, tcm, sipTCM[%d].isUsing = false", i);

            	memset(&sipTCM[i], 0, sizeof(sipTpTcm_t));
            	sipTCM[i].sockfd = -1;

				//update quarantine list
				int i=0;
				for(i=0; i<sipTpMaxQNum; i++)
				{
					if(!sipTpQList[i].isUsing)
					{
						sipTpQList[i].isUsing = true;
						sipTpQList[i].peer = sipTCM[i].peer;
						sipTpQList[i].qTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME, sipTpOnQTimeout, &sipTpQList[i]);
						break;
					}
				}

				if( i == sipTpMaxQNum)
				{
					if(sipTpMaxQNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
					{
                        sipTpQList[i].isUsing = true;
                        sipTpQList[i].peer = sipTCM[i].peer;
                        sipTpQList[i].qTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME, sipTpOnQTimeout, &sipTpQList[i]);
						sipTpMaxQNum++;
					}
					else
					{
						logError("maximum number of qNum (%d) exceeds SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM.", sipTpMaxQNum);
					}
				}
			}

            goto EXIT;
        }
	}
	
	logError("fails to find a matching tcm, tcpfd = %d.", tcpfd);
	status = OS_ERROR_INVALID_VALUE;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return status;
}


bool sipTpIsInQList(struct sockaddr_in peer)
{
	bool isFound = false;

    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return isFound;
    }

	for(int i=0; i<sipTpMaxQNum; i++)
	{
    	if(!sipTpQList[i].isUsing)
		{
			continue;
		}

		if((sipTpQList[i].peer.sin_port == peer.sin_port) && (sipTpQList[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr))
		{
			isFound = true;
			break;
		}
	}

    pthread_rwlock_unlock(&tcmRwlock);
	return isFound;
}


static void sipTpOnKATimeout(uint64_t timerId, void* ptr)
{
    sipTpTcm_t* pTcm = ptr;

	mdebug(LM_TRANSPORT, "timerId=0x%x expired.", timerId);

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return;
    }

    if(pTcm->tcpKeepAlive.keepAliveTimerId != timerId)
    {
        logError("timeout, but the returned timerId (%ld) does not match the local timerId (%ld).", timerId, pTcm->tcpKeepAlive.keepAliveTimerId);
        goto EXIT;
    }

    if(pTcm->tcpKeepAlive.isUsed)
    {
		mdebug(LM_TRANSPORT, "tcpFd(%d) was used during the keep alive period.", pTcm->sockfd);

        pTcm->tcpKeepAlive.isUsed = false;
        pTcm->tcpKeepAlive.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, pTcm);
    }
    else
    {
		mdebug(LM_TRANSPORT, "tcpFd(%d) has not been used during the keep alive period(%d ms), close the fd.", pTcm->sockfd, SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE);

        pTcm->isUsing = false;
logError("to-remove, tcm, sipTCM.isUsing = false");
		close(pTcm->sockfd);

		//re-initiate the pTcm to all 0
		memset(pTcm, 0, sizeof(sipTpTcm_t));
		pTcm->sockfd = -1;
    }

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return;
}


osMBuf_t* sipTpTcmBufInit(sipTpBuf_t* pTpBuf, bool isAllocSipBuf)
{
    pTpBuf->state.eomPattern = SIP_TRANSPORT_EOM_OTHER;
    pTpBuf->state.clValue = -1;
    pTpBuf->state.isBadMsg = false;

	pTpBuf->pSipMsgBuf = isAllocSipBuf ? osMBuf_alloc(SIP_CONFIG_TRANSPORT_TCP_BUFFER_SIZE) : NULL;

	if(!pTpBuf->pSipMsgBuf)
	{
		logError("pTpBuf->pSipMsgBuf is NULL.");
	}
	else
	{
		pTpBuf->pSipMsgBuf->pos = 0;
		pTpBuf->pSipMsgBuf->end = 0;
	}

    return pTpBuf->pSipMsgBuf;
}


static void sipTpOnQTimeout(uint64_t timerId, void* ptr)
{
    sipTpQuarantine_t* pTpQ = ptr;
    if(!pTpQ)
    {
        logError("null pointer, ptr.");
        return;
    }

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return;
    }

    if(pTpQ->qTimerId != timerId)
    {
        logError("timeout, but the returned timerId (%ld) does not match the local timerId (%ld).", timerId, pTpQ->qTimerId);
        goto EXIT;
    }

    pTpQ->isUsing = false;
logError("to-remove, tcm, sipTCM.isUsing = false");

    pTpQ->qTimerId = 0;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return;
}


//only list if the LM_TRANSPORT is configured on DEBUG level
void sipTpListUsedTcm()
{
    if(osDbg_isBypass(DBG_DEBUG, LM_TRANSPORT))
    {
        return;
    }

    sipTpTcm_t* pTcm = NULL;

#if 0
    struct sockaddr_in peer = {};

    peer.sin_port = htons(peerPort);
    if (inet_pton(AF_INET, peerIp, &peer.sin_addr.s_addr) == 0)
    {
        logError("inet_pton for peerIp=%s, errno=%d.", peerIp, errno);
        goto EXIT;
    }
#endif

    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return;
    }

	//size of 600 is based on 10 * each_tcm_print_size (assume <60).  If each_tcm_print_size becomes bigger, the allocated buffer size shall also change
	char prtBuffer[600];
    int i=0, count=0, n=0;

    for(i=0; i<sipTcmMaxNum; i++)
    {
        if(sipTCM[i].isUsing)
        {
			n += sprintf(&prtBuffer[n], "i=%d, sockfd=%d, peer IP:port=%s:%d\n", i, sipTCM[i].sockfd, inet_ntoa(sipTCM[i].peer.sin_addr), ntohs(sipTCM[i].peer.sin_port));
			if(++count == 10)
        	{
            	prtBuffer[n] = 0;
				mdebug(LM_TRANSPORT, "used TCM list:\n%s", prtBuffer);
				n = 0;
				count = 0;
			}
        }
    }

	if(count >0)
	{
		prtBuffer[n] = 0;
		mdebug(LM_TRANSPORT, "used TCM list:\n%s", prtBuffer);
	}
	else if(i == 0)
	{
		mdebug(LM_TRANSPORT, "used TCM list:\nNo used TCM.");
	}

    pthread_rwlock_unlock(&tcmRwlock);
}
