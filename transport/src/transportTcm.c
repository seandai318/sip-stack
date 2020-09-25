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
#include <sys/epoll.h>

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
#include "tcm.h"
#include "transportConfig.h"
#include "transportIntf.h"



static void sipTpOnKATimeout(uint64_t timerId, void* ptr);
static void sipTpOnQTimeout(uint64_t timerId, void* ptr);


//this is shared by both server and client/transport threads
static tpTcm_t sipTCM[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static uint8_t sipTcmMaxNum = 0;

//in TpServer.c, pTcm->msgConnInfo.is modified and is not Rwlock protected, it is assumed nobody messes with this field except TpServer.c, the same for TpClient.c
static pthread_rwlock_t tcmRwlock;
static __thread tpQuarantine_t sipTpQList[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static __thread uint8_t sipTpMaxQNum = 0;
static  notifyTcpConnUser_h notifyTcpConnUser[TRANSPORT_APP_TYPE_COUNT];


void tcmInit(notifyTcpConnUser_h notifier[], int notifyNum)
{
	if(notifyNum > TRANSPORT_APP_TYPE_COUNT)
	{
		logError("notifyNum(%d) exceeds the allowed value(%d).", notifyNum, TRANSPORT_APP_TYPE_COUNT);
		return;
	}

	for(int i=0; i<notifyNum; i++)
	{
		notifyTcpConnUser[i] = notifier[i];
	}

	pthread_rwlock_init(&tcmRwlock,NULL);
}


//isReserveOne=true will reserve a tcm if a matching is not found
tpTcm_t* tpGetTcm(struct sockaddr_in peer, transportAppType_e appType, bool isReseveOne)
{
	tpTcm_t* pTcm = NULL;

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

		if((sipTCM[i].peer.sin_port == peer.sin_port) && (sipTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr) && (sipTCM[i].appType == appType))
		{
			pTcm = &sipTCM[i];
			isMatch = true;
			goto EXIT;
		}
	}

	if(!pTcm && isReseveOne)
	{
		if(sipTcmMaxNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
		{
			pTcm = &sipTCM[sipTcmMaxNum++];
		}
	}

	//new reserved TCP set to isUsing = true
	if(pTcm)
	{
		pTcm->peer = peer;
		pTcm->sockfd = -1;
		pTcm->isUsing = true;
	}

EXIT:
	pthread_rwlock_unlock(&tcmRwlock);

	if(!isMatch && pTcm != NULL)
	{
		pTcm->appType = appType;
	}

	return pTcm;
}


void tpReleaseTcm(tpTcm_t* pTcm)
{
	if(!pTcm)
	{
		return;
	}

    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return;
    }

	pTcm->isUsing = false;
	pTcm->sockfd = -1;
    memset(pTcm, 0, sizeof(tpTcm_t));

    pthread_rwlock_unlock(&tcmRwlock);
}



tpTcm_t* tpGetTcmByFd(int tcpFd, struct sockaddr_in peer)
{
    tpTcm_t* pTcm = NULL;

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



tpTcm_t* tpGetConnectedTcm(int tcpFd)
{
    tpTcm_t* pTcm = NULL;

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
			if(!pTcm->msgConnInfo.pMsgBuf)
			{
				pTcm->msgConnInfo.pMsgBuf = osMBuf_alloc(tpGetBufSize(pTcm->appType));
				if(!pTcm->msgConnInfo.pMsgBuf)
				{
					logError("fails to allocate memory for pTcm->msgConnInfo.sipBuf->pMsgBuf.");
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


osStatus_e tpDeleteTcm(int tcpfd, bool isNotifyApp)
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
			debug("set sipTCM[%d].isUsing = false", i);
			if(sipTCM[i].isTcpConnDone)
			{
				osStopTimer(sipTCM[i].msgConnInfo.keepAliveTimerId);
				sipTCM[i].msgConnInfo.keepAliveTimerId = 0;
				if(sipTCM[i].msgConnInfo.pMsgBuf)
				{
					osMBuf_dealloc(sipTCM[i].msgConnInfo.pMsgBuf);
					sipTCM[i].msgConnInfo.pMsgBuf = NULL;
				}

				sipTCM[i].isTcpConnDone = false;
			}
			else
			{
				mdebug(LM_TRANSPORT, "sipTCM[i].appType=%d, notifyTcpConnUser[sipTCM[i].appType]=%p, i=%d.", sipTCM[i].appType, notifyTcpConnUser[sipTCM[i].appType], i);

    			if(isNotifyApp && notifyTcpConnUser[sipTCM[i].appType])
    			{
        			notifyTcpConnUser[sipTCM[i].appType](&sipTCM[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, tcpfd, NULL);
    			}
			}

			memset(&sipTCM[i], 0, sizeof(tpTcm_t));
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


void tpDeleteAllTcm()
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
        mdebug(LM_TRANSPORT, "tcm, sipTCM[%d].isUsing = false", i);

        if(sipTCM[i].isTcpConnDone)
        {
            osStopTimer(sipTCM[i].msgConnInfo.keepAliveTimerId);
            sipTCM[i].msgConnInfo.keepAliveTimerId = 0;
            if(sipTCM[i].msgConnInfo.pMsgBuf)
            {
                osMBuf_dealloc(sipTCM[i].msgConnInfo.pMsgBuf);
                sipTCM[i].msgConnInfo.pMsgBuf = NULL;
        	}  

            sipTCM[i].isTcpConnDone = false;
		}
        else
        {
			mdebug(LM_TRANSPORT, "sipTCM[i].appType=%d, notifyTcpConnUser[sipTCM[i].appType]=%p, i=%d.", sipTCM[i].appType, notifyTcpConnUser[sipTCM[i].appType], i);

	        if(notifyTcpConnUser[sipTCM[i].appType])
            {
                notifyTcpConnUser[sipTCM[i].appType](&sipTCM[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, -1, NULL);
            }
        }

		close(sipTCM[i].sockfd);

        memset(&sipTCM[i], 0, sizeof(tpTcm_t));
        sipTCM[i].sockfd = -1;
    }

    pthread_rwlock_unlock(&tcmRwlock);
}


osStatus_e tpTcmAddUser(tpTcm_t* pTcm, void* pTrId)
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

	osListPlus_append(&pTcm->tcpConn.appIdList, pTrId);

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return status;
}


osStatus_e tpTcmAddFd(int tcpfd, struct sockaddr_in* peer, transportAppType_e appType)
{
    osStatus_e status = OS_STATUS_OK;
	tpTcm_t* pTcm = NULL;

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
		pTcm->appType = appType;
		pTcm->sockfd = tcpfd;
		pTcm->peer = *peer;
        tpTcmBufInit(pTcm, true);

		mdebug(LM_TRANSPORT, "pTcm=%p set to true, pTcm->appType=%d, notifyTcpConnUser[pTcm->appType]=%p.", pTcm, pTcm->appType, notifyTcpConnUser[pTcm->appType]);

	    if(notifyTcpConnUser[pTcm->appType])
    	{
			//for TRANSPORT_STATUS_TCP_SERVER_OK, there is no appId, as it is server receiving the connection request
        	notifyTcpConnUser[pTcm->appType](NULL, TRANSPORT_STATUS_TCP_SERVER_OK, tcpfd, peer);
    	}

		switch(appType)
		{
			case TRANSPORT_APP_TYPE_SIP:
				pTcm->msgConnInfo.isPersistent = false;
				pTcm->msgConnInfo.isUsed = false;
				pTcm->msgConnInfo.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, pTcm);
				break;
			case TRANSPORT_APP_TYPE_DIAMETER:
			default:
				pTcm->msgConnInfo.isPersistent = true;
				break;
		}

		mdebug(LM_TRANSPORT, "tcpfd(%d, %A) is added to pTcm(%p)", tcpfd, &peer, pTcm);  
		goto EXIT;
	}

	status = OS_ERROR_INVALID_VALUE;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	tpListUsedTcm();

	return status;
}


//update TCM conn status for a waiting for conn TCM
osStatus_e tpTcmUpdateConn(int tcpfd, bool isConnEstablished)
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
			if(sipTCM[i].isTcpConnDone && isConnEstablished)
			{
				logError("try to update a TCP connection status for fd (%d), but tcm(%p) shows isTcpConnDone = true.", tcpfd, &sipTCM[i]);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			if(isConnEstablished)
			{
				sipTCM[i].isTcpConnDone = true;

				mdebug(LM_TRANSPORT, "sipTCM[i].appType=%d, notifyTcpConnUser[sipTCM[i].appType]=%p, i=%d.", sipTCM[i].appType, notifyTcpConnUser[sipTCM[i].appType], i); 
				if(notifyTcpConnUser[sipTCM[i].appType])
				{
					notifyTcpConnUser[sipTCM[i].appType](&sipTCM[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_OK, tcpfd, &sipTCM[i].peer);
				}
				sipTCM[i].msgConnInfo.pMsgBuf = osMBuf_alloc(tpGetBufSize(sipTCM[i].appType));
				if(sipTCM[i].appType == TRANSPORT_APP_TYPE_SIP)
				{
                	sipTCM[i].msgConnInfo.isPersistent = false;
					sipTCM[i].msgConnInfo.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, &sipTCM[i]);
				}
				else
				{
					sipTCM[i].msgConnInfo.isPersistent = true;
				}
			}
			else
			{
				if(notifyTcpConnUser[sipTCM[i].appType])
				{
                	notifyTcpConnUser[sipTCM[i].appType](&sipTCM[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, -1, &sipTCM[i].peer);
				}

            	sipTCM[i].isUsing = false;
				mdebug(LM_TRANSPORT, "sipTCM[%d].isUsing = false", i);

            	memset(&sipTCM[i], 0, sizeof(tpTcm_t));
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


bool tpIsInQList(struct sockaddr_in peer)
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
    tpTcm_t* pTcm = ptr;

	mdebug(LM_TRANSPORT, "timerId=0x%x expired.", timerId);

    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return;
    }

    if(pTcm->msgConnInfo.keepAliveTimerId != timerId)
    {
        logError("timeout, but the returned timerId (%ld) does not match the local timerId (%ld).", timerId, pTcm->msgConnInfo.keepAliveTimerId);
        goto EXIT;
    }

    if(pTcm->msgConnInfo.isUsed)
    {
		mdebug(LM_TRANSPORT, "tcpFd(%d) was used during the keep alive period.", pTcm->sockfd);

        pTcm->msgConnInfo.isUsed = false;
        pTcm->msgConnInfo.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, pTcm);
    }
    else
    {
		mdebug(LM_TRANSPORT, "tcpFd(%d) has not been used during the keep alive period(%d ms), set sipTCM.isUsing = false, close the fd.", pTcm->sockfd, SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE);

        pTcm->isUsing = false;
		close(pTcm->sockfd);

		//re-initiate the pTcm to all 0
		memset(pTcm, 0, sizeof(tpTcm_t));
		pTcm->sockfd = -1;
    }

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return;
}


osMBuf_t* tpTcmBufInit(tpTcm_t* pTcm, bool isAllocBuf)
{
	pTcm->msgConnInfo.pMsgBuf = NULL;

	size_t bufSize = 0;
	switch(pTcm->appType)
	{
		case TRANSPORT_APP_TYPE_SIP:
		{
	    	pTcm->msgConnInfo.sipState.eomPattern = SIP_TRANSPORT_EOM_OTHER;
    		pTcm->msgConnInfo.sipState.clValue = -1;
    		pTcm->msgConnInfo.sipState.isBadMsg = false;

			break;
		}
		case TRANSPORT_APP_TYPE_DIAMETER:
		{
			pTcm->msgConnInfo.diaState.isBadMsg = false;
			pTcm->msgConnInfo.diaState.msgLen = 0;
			pTcm->msgConnInfo.diaState.receivedBytes = 0;

            break;
        }
		default:
			logError("appType(%d) is not handled.", pTcm->appType);
			goto EXIT;
			break;
	}

    if(isAllocBuf)
	{
		pTcm->msgConnInfo.pMsgBuf = osMBuf_alloc(tpGetBufSize(pTcm->appType));
	}

    if(!pTcm->msgConnInfo.pMsgBuf)
    {
        logError("fails to allocate pTcm->msgConnInfo.pMsgBuf.");
    }
    else
    {
        pTcm->msgConnInfo.pMsgBuf->pos = 0;
        pTcm->msgConnInfo.pMsgBuf->end = 0;
    }

EXIT:
	return pTcm->msgConnInfo.pMsgBuf;
}


static void sipTpOnQTimeout(uint64_t timerId, void* ptr)
{
    tpQuarantine_t* pTpQ = ptr;
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
	mdebug(LM_TRANSPORT, "sipTCM.isUsing = false");

    pTpQ->qTimerId = 0;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return;
}


//only list if the LM_TRANSPORT is configured on DEBUG level
void tpListUsedTcm()
{
    if(osDbg_isBypass(DBG_DEBUG, LM_TRANSPORT))
    {
        return;
    }

    tpTcm_t* pTcm = NULL;

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


osStatus_e tpTcmCloseTcpConn(int tpEpFd, int tcpFd, bool isNotifyApp)
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
