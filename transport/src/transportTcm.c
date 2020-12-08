/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportTcm.c
 ********************************************************/

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
static tpTcm_t* tpGetConnectedTcmInternal(int tcpFd, tpTcm_t* tcmList, uint8_t maxTcmMaxNum);
static osStatus_e tpDeleteTcmInternal(int tcpfd, bool isNotifyApp, tpTcm_t* tcmList, uint8_t maxTcmMaxNum);
static osStatus_e  tpDeleteTcm(int tcpFd, bool isNotifyApp, bool isCom);
static void tpDeleteAllTcmInternal(tpTcm_t* tcmList, uint8_t maxTcmMaxNum);
static osStatus_e tpTcmUpdateConnInternal(int tcpfd, bool isConnEstablished, tpTcm_t* tcmList, uint8_t tcmMaxNum);
static void tpListUsedTcmInternal(tpTcm_t* tcmList, uint8_t tcmMaxNum, char* tcmName);


//this is shared by both server and client/transport threads, created by lister thread (COM thread)
static tpTcm_t gSharedTCM[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static __thread uint8_t gSharedTcmMaxNum = 0;

//this is per thread
static __thread tpTcm_t gAppTCM[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static __thread uint8_t gAppTcmMaxNum = 0;

//in TpServer.c, pTcm->msgConnInfo.is modified and is not Rwlock protected, it is assumed nobody messes with this field except TpServer.c, the same for TpClient.c
static pthread_rwlock_t tcmRwlock;
static tpQuarantine_t sipTpQList[SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM] = {};
static uint8_t sipTpMaxQNum = 0;
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


//this function is called when there is request from app to send a message
//first check if the shared TCM list has matching TCM, if not, check per thread TCM
//isReserveOne=true will reserve a tcm if a matching is not found
tpTcm_t* tpGetTcm4SendMsg(struct sockaddr_in peer, transportAppType_e appType, bool isReseveOne, bool* isTcpConnOngoing)
{
	tpTcm_t* pTcm = NULL;
	bool isMatch = false;
	*isTcpConnOngoing = false;

	//first check the shared TCM, i.e., TCM created by listening COM
    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return NULL;
    }

	for(int i=0; i<gSharedTcmMaxNum; i++)
    {
        if(!gSharedTCM[i].isUsing)
        {
            continue;
        }

        //needs to have pTcm->sockfd check since there may have multiple TCM per dest per app.  it is possible simultaneously multiple requests to create TCM.
        if((gSharedTCM[i].peer.sin_port == peer.sin_port) && (gSharedTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr) && (gSharedTCM[i].appType == appType) && pTcm->sockfd != -1)
        {
            pTcm = &gSharedTCM[i];
			isMatch = true;
			break;
        }
    }

    pthread_rwlock_unlock(&tcmRwlock);

	if(isMatch)
	{
		goto EXIT;
	}

	//if there is no connection in the shared TCM, check per thread TCM
	for(int i=0; i<gAppTcmMaxNum; i++)
	{
		if(!gAppTCM[i].isUsing)
		{
			if(isReseveOne && !pTcm)
			{
				pTcm = &gAppTCM[i];
			}
			continue;
		}

		//needs to have pTcm->sockfd check since there may have multiple TCM per dest per app.  it is possible simultaneously multiple requests to create TCM.
		if((gAppTCM[i].peer.sin_port == peer.sin_port) && (gAppTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr) && (gAppTCM[i].appType == appType))
		{
			pTcm = &gSharedTCM[i];
			isMatch = true;

			if(pTcm->sockfd != -1)
			{
				*isTcpConnOngoing = true;
			}

			goto EXIT;
		}
	}

	if(!pTcm && isReseveOne)
	{
		if(gAppTcmMaxNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
		{
			pTcm = &gAppTCM[gAppTcmMaxNum++];
		}
	}

	//new reserved TCP set to isUsing = true
	if(pTcm)
	{
		pTcm->peer = peer;
		pTcm->sockfd = -1;
		pTcm->isUsing = true;
		mdebug(LM_TRANSPORT, "pTcm(%p).isUsing=true, appType=%d.", pTcm, appType);
	}

EXIT:
	if(!isMatch && pTcm != NULL)
	{
		pTcm->appType = appType;
	}

	return pTcm;
}


void tpReleaseAppTcm(tpTcm_t* pTcm)
{
	if(!pTcm)
	{
		return;
	}

#if 0
    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return;
    }
#endif
	pTcm->isUsing = false;
    mdebug(LM_TRANSPORT, "pTcm(%p).isUsing=false, fd=%d.", pTcm, pTcm->sockfd);

	pTcm->sockfd = -1;
    memset(pTcm, 0, sizeof(tpTcm_t));

#if 0
    pthread_rwlock_unlock(&tcmRwlock);
#endif
}



tpTcm_t* tpGetTcmByFd(int tcpFd, struct sockaddr_in peer)
{
    tpTcm_t* pTcm = NULL;

	//first check the shared TCM list
    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return NULL;
    }

    for(int i=0; i<gSharedTcmMaxNum; i++)
    {
        if(gSharedTCM[i].isUsing)
        {
			if(gSharedTCM[i].sockfd == tcpFd)
			{ 
        		if((gSharedTCM[i].peer.sin_port == peer.sin_port) && (gSharedTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr))
        		{
            		pTcm = &gSharedTCM[i];
            		break;
        		}
    		}
		}
	}

    pthread_rwlock_unlock(&tcmRwlock);

	//if there is no matching fd in the shared TCM list, check the per thread TCM list
	if(!pTcm)
	{
	    for(int i=0; i<gAppTcmMaxNum; i++)
    	{
        	if(gAppTCM[i].isUsing)
        	{
            	if(gAppTCM[i].sockfd == tcpFd)
            	{
                	if((gAppTCM[i].peer.sin_port == peer.sin_port) && (gAppTCM[i].peer.sin_addr.s_addr == peer.sin_addr.s_addr))
                	{
                    	pTcm = &gAppTCM[i];
                    	break;
                	}
				}
            }
        }
    }

EXIT:
	return pTcm;
}


//if isCom = true, check in shared TCM list, otherwise, in per thread TCM list
tpTcm_t* tpGetConnectedTcm(int tcpFd, bool isCom)
{
    tpTcm_t* pTcm = NULL;
    bool isMatch = false;

	if(isCom)
	{
    	if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    	{
        	logError("fails to acquire wrlock for tcmRwlock.");
        	return NULL;
    	}

		pTcm = tpGetConnectedTcmInternal(tcpFd, gSharedTCM, gSharedTcmMaxNum);
	
    	pthread_rwlock_unlock(&tcmRwlock);
	}
	else
	{
		pTcm = tpGetConnectedTcmInternal(tcpFd, gAppTCM, gAppTcmMaxNum);
	}

EXIT:
	return pTcm;
}	


static tpTcm_t* tpGetConnectedTcmInternal(int tcpFd, tpTcm_t* tcmList, uint8_t maxTcmMaxNum)
{
	tpTcm_t* pTcm = NULL;

    for(int i=0; i<maxTcmMaxNum; i++)
    {
        if(!tcmList[i].isUsing)
        {
            continue;
        }

        if(tcmList[i].sockfd == tcpFd)
        {
            pTcm = &tcmList[i];
            if(!pTcm->msgConnInfo.pMsgBuf)
            {
                pTcm->msgConnInfo.pMsgBuf = osMBuf_alloc(tpGetBufSize(pTcm->appType));
                if(!pTcm->msgConnInfo.pMsgBuf)
                {
                    logError("fails to allocate memory for pTcm->msgConnInfo.sipBuf->pMsgBuf.");

                    pTcm = NULL;
                    goto EXIT;
                }
            }
        }
    }

EXIT:
    return pTcm;
}

	
static osStatus_e tpDeleteTcm(int tcpfd, bool isNotifyApp, bool isCom)
{
    osStatus_e status = OS_STATUS_OK;

    if(isCom)
    {
        if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
        {
            logError("fails to acquire wrlock for tcmRwlock.");
            status = OS_ERROR_SYSTEM_FAILURE;
        }

        status = tpDeleteTcmInternal(tcpfd, isNotifyApp, gSharedTCM, gSharedTcmMaxNum);

        pthread_rwlock_unlock(&tcmRwlock);
    }
    else
    {
        status = tpDeleteTcmInternal(tcpfd, isNotifyApp, gAppTCM, gAppTcmMaxNum);
    }

	return status;
}


static osStatus_e tpDeleteTcmInternal(int tcpfd, bool isNotifyApp, tpTcm_t* tcmList, uint8_t maxTcmMaxNum)
{
    osStatus_e status = OS_STATUS_OK;

    for(int i=0; i< maxTcmMaxNum; i++)
    {
        if(!tcmList[i].isUsing)
        {
            continue;
        }

        if(tcmList[i].sockfd == tcpfd)
        {
			tcmList[i].isUsing = false;
			mdebug(LM_TRANSPORT, "pTcm(%p, tcmList[%d]).isUsing=false, tcpfd=%d", &tcmList[i], i, tcpfd);
			if(tcmList[i].isTcpConnDone)
			{
				osStopTimer(tcmList[i].msgConnInfo.keepAliveTimerId);
				tcmList[i].msgConnInfo.keepAliveTimerId = 0;
				if(tcmList[i].msgConnInfo.pMsgBuf)
				{
					osMBuf_dealloc(tcmList[i].msgConnInfo.pMsgBuf);
					tcmList[i].msgConnInfo.pMsgBuf = NULL;
				}

				tcmList[i].isTcpConnDone = false;
			}
			else
			{
				mdebug(LM_TRANSPORT, "tcmList[i].appType=%d, notifyTcpConnUser[tcmList[i].appType]=%p, i=%d.", tcmList[i].appType, notifyTcpConnUser[tcmList[i].appType], i);

    			if(isNotifyApp && notifyTcpConnUser[tcmList[i].appType])
    			{
        			notifyTcpConnUser[tcmList[i].appType](&tcmList[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, tcpfd, NULL);
    			}
			}

			memset(&tcmList[i], 0, sizeof(tpTcm_t));
			tcmList[i].sockfd = -1;

            goto EXIT;
        }
    }

    logError("fails to find a matching tcm, tcpfd = %d.", tcpfd);
    status = OS_ERROR_INVALID_VALUE;

EXIT:
    return status;
}


void tpDeleteAllTcm()
{
    if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire wrlock for tcmRwlock.");
        return;
    }

	tpDeleteAllTcmInternal(gSharedTCM, gSharedTcmMaxNum);

    pthread_rwlock_unlock(&tcmRwlock);

    tpDeleteAllTcmInternal(gAppTCM, gAppTcmMaxNum);
}


static void tpDeleteAllTcmInternal(tpTcm_t* tcmList, uint8_t maxTcmMaxNum)
{
    for(int i=0; i<maxTcmMaxNum; i++)
    {
        if(!tcmList[i].isUsing)
        {
            continue;
        }

        tcmList[i].isUsing = false;
        mdebug(LM_TRANSPORT, "pTcm(%p, tcmList[%d]).isUsing=false, fd=%d", &tcmList[i], i, tcmList[i].sockfd);

        if(tcmList[i].isTcpConnDone)
        {
            osStopTimer(tcmList[i].msgConnInfo.keepAliveTimerId);
            tcmList[i].msgConnInfo.keepAliveTimerId = 0;
            if(tcmList[i].msgConnInfo.pMsgBuf)
            {
                osMBuf_dealloc(tcmList[i].msgConnInfo.pMsgBuf);
                tcmList[i].msgConnInfo.pMsgBuf = NULL;
        	}  

            tcmList[i].isTcpConnDone = false;
		}
        else
        {
			mdebug(LM_TRANSPORT, "tcmList[i].appType=%d, notifyTcpConnUser[tcmList[i].appType]=%p, i=%d.", tcmList[i].appType, notifyTcpConnUser[tcmList[i].appType], i);

	        if(notifyTcpConnUser[tcmList[i].appType])
            {
                notifyTcpConnUser[tcmList[i].appType](&tcmList[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, -1, NULL);
            }
        }

		close(tcmList[i].sockfd);

        memset(&tcmList[i], 0, sizeof(tpTcm_t));
        tcmList[i].sockfd = -1;
    }
}


//no need to use tcmRwlock in this function since the TCM in this function shall be per thread
osStatus_e tpAppTcmAddUser(tpTcm_t* pTcm, void* pTrId)
{
    osStatus_e status = OS_STATUS_OK;

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
	return status;
}


osStatus_e tpComTcmAddFd(int tcpfd, struct sockaddr_in* peer, struct sockaddr_in* local, transportAppType_e appType)
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

    for(int i=0; i<gSharedTcmMaxNum; i++)
    {
        if(!gSharedTCM[i].isUsing)
        {
			pTcm = &gSharedTCM[i];
			break;
		}
	}

    if(!pTcm)
    {
        if(gSharedTcmMaxNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
        {
            pTcm = &gSharedTCM[gSharedTcmMaxNum++];
        }
    }

	if(pTcm)
	{
		pTcm->isUsing = true;
		pTcm->isTcpConnDone = true;
		pTcm->appType = appType;
		pTcm->sockfd = tcpfd;
		pTcm->peer = *peer;
		pTcm->local = *local;
        tpTcmBufInit(pTcm, true);

		mdebug(LM_TRANSPORT, "pTcm(%p).isUsing=true, fd=%d, pTcm->appType=%d, notifyTcpConnUser[pTcm->appType]=%p.", pTcm, tcpfd, pTcm->appType, notifyTcpConnUser[pTcm->appType]);

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

		mdebug(LM_TRANSPORT, "tcpfd(%d, %A) is added to pTcm(%p)", tcpfd, peer, pTcm); 
		goto EXIT;
	}

	status = OS_ERROR_INVALID_VALUE;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	tpListUsedTcm(true);

	return status;
}


//update TCM conn status for a waiting for conn TCM
//this shall only be called by per thread TCP connection creation, as COM is purely TCP connection listener.  Nevertheless, this function supports COM TCP connection creation
osStatus_e tpTcmUpdateConn(int tcpfd, bool isConnEstablished, bool isCom)
{
    osStatus_e status = OS_STATUS_OK;

	if(isCom)
	{
    	if(pthread_rwlock_wrlock(&tcmRwlock) != 0)
    	{
        	logError("fails to acquire wrlock for tcmRwlock.");
        	return OS_ERROR_SYSTEM_FAILURE;
    	}

		status = tpTcmUpdateConnInternal(tcpfd, isConnEstablished, gSharedTCM, gSharedTcmMaxNum);

		pthread_rwlock_unlock(&tcmRwlock);
	}
	else
	{
		status = tpTcmUpdateConnInternal(tcpfd, isConnEstablished, gAppTCM, gAppTcmMaxNum);
	}

	return status;
}


static osStatus_e tpTcmUpdateConnInternal(int tcpfd, bool isConnEstablished, tpTcm_t* tcmList, uint8_t tcmMaxNum)
{
	osStatus_e status = OS_STATUS_OK;

    for(int i=0; i<tcmMaxNum; i++)
    {
        if(!tcmList[i].isUsing)
        {
			continue;
        }

        if(tcmList[i].sockfd == tcpfd)
        {
			if(tcmList[i].isTcpConnDone && isConnEstablished)
			{
				logError("try to update a TCP connection status for fd (%d), but tcm(%p) shows isTcpConnDone = true.", tcpfd, &tcmList[i]);
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}

			if(isConnEstablished)
			{
				tcmList[i].isTcpConnDone = true;

				mdebug(LM_TRANSPORT, "tcmList[i].appType=%d, notifyTcpConnUser[tcmList[i].appType]=%p, i=%d.", tcmList[i].appType, notifyTcpConnUser[tcmList[i].appType], i); 
				if(notifyTcpConnUser[tcmList[i].appType])
				{
					notifyTcpConnUser[tcmList[i].appType](&tcmList[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_OK, tcpfd, &tcmList[i].peer);
				}
				tcmList[i].msgConnInfo.pMsgBuf = osMBuf_alloc(tpGetBufSize(tcmList[i].appType));
				if(tcmList[i].appType == TRANSPORT_APP_TYPE_SIP)
				{
                	tcmList[i].msgConnInfo.isPersistent = false;
					tcmList[i].msgConnInfo.keepAliveTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE, sipTpOnKATimeout, &tcmList[i]);
				}
				else
				{
					tcmList[i].msgConnInfo.isPersistent = true;
				}
			}
			else
			{
				if(notifyTcpConnUser[tcmList[i].appType])
				{
                	notifyTcpConnUser[tcmList[i].appType](&tcmList[i].tcpConn.appIdList, TRANSPORT_STATUS_TCP_FAIL, -1, &tcmList[i].peer);
				}

            	tcmList[i].isUsing = false;
				mdebug(LM_TRANSPORT, "pTcm(%p, tcmList[%d]).isUsing=false, fd=%d.", &tcmList[i], i, tcpfd);

            	memset(&tcmList[i], 0, sizeof(tpTcm_t));
            	tcmList[i].sockfd = -1;

				//update quarantine list
				int i=0;
				for(i=0; i<sipTpMaxQNum; i++)
				{
					if(!sipTpQList[i].isUsing)
					{
						sipTpQList[i].isUsing = true;
						mdebug(LM_TRANSPORT, "pTpQ(%p, sipTpQList[%d]).isUsing=true.", &sipTpQList[i], i);

						sipTpQList[i].peer = tcmList[i].peer;
						sipTpQList[i].qTimerId = osStartTimer(SIP_CONFIG_TRANSPORT_TCP_CONN_QUARANTINE_TIME, sipTpOnQTimeout, &sipTpQList[i]);
						break;
					}
				}

				if( i == sipTpMaxQNum)
				{
					if(sipTpMaxQNum < SIP_CONFIG_TRANSPORT_MAX_TCP_PEER_NUM)
					{
                        sipTpQList[i].isUsing = true;
                        mdebug(LM_TRANSPORT, "pTpQ(%p, sipTpQList[%d]).isUsing=true.", &sipTpQList[i], i);

                        sipTpQList[i].peer = tcmList[i].peer;
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
        logError("timeout, but the returned timerId (0x%lx) does not match the local timerId (0x%lx).", timerId, pTcm->msgConnInfo.keepAliveTimerId);
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
		mdebug(LM_TRANSPORT, "pTcm(%p).isUsing=false, tcpFd(%d) has not been used during the keep alive period(%d ms), close the fd.", pTcm, pTcm->sockfd, SIP_CONFIG_TRANSPORT_MAX_TCP_CONN_ALIVE);

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
	mdebug(LM_TRANSPORT, "pTpQ(%p).isUsing=false", pTpQ);

    pTpQ->qTimerId = 0;

EXIT:
    pthread_rwlock_unlock(&tcmRwlock);
	return;
}


//only list if the LM_TRANSPORT is configured on DEBUG level
//always display shared TCM, app TCM display depends on isCom parameter
void tpListUsedTcm(bool isCom)
{
    if(osDbg_isBypass(DBG_DEBUG, LM_TRANSPORT))
    {
        return;
    }

    if(pthread_rwlock_rdlock(&tcmRwlock) != 0)
    {
        logError("fails to acquire rdlock for tcmRwlock.");
        return;
    }

    pthread_rwlock_unlock(&tcmRwlock);

    if(!isCom)
    {
    	tpListUsedTcmInternal(gSharedTCM, gSharedTcmMaxNum, "app");
	}
}


static void tpListUsedTcmInternal(tpTcm_t* tcmList, uint8_t tcmMaxNum, char* tcmName)
{
    tpTcm_t* pTcm = NULL;

	//size of 600 is based on 10 * each_tcm_print_size (assume <60).  If each_tcm_print_size becomes bigger, the allocated buffer size shall also change
	char prtBuffer[600];
    int i=0, count=0, n=0;

    for(i=0; i<tcmMaxNum; i++)
    {
        if(tcmList[i].isUsing)
        {
			n += sprintf(&prtBuffer[n], "i=%d, pTcm=%p, sockfd=%d, peer IP:port=%s:%d\n", i, &tcmList[i], tcmList[i].sockfd, inet_ntoa(tcmList[i].peer.sin_addr), ntohs(tcmList[i].peer.sin_port));
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
		mdebug(LM_TRANSPORT, "%s used TCM list:\n%s", tcmName, prtBuffer);
	}
	else if(i == 0)
	{
		mdebug(LM_TRANSPORT, "%s used TCM list:\nNo used TCM.", tcmName);
	}
}


osStatus_e tpTcmCloseTcpConn(int tpEpFd, int tcpFd, bool isNotifyApp, bool isCom)
{
    DEBUG_BEGIN

    osStatus_e status = tpDeleteTcm(tcpFd, isNotifyApp, isCom);
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
