/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportUdpMgmt.c
 * This file manages the UDP sockets that perform both send and receive.
 * a tpUdpInfoList list maintains all udp fds, one udp per a appType + local interface
 ********************************************************/


#include <sys/epoll.h>
#include <unistd.h>

#include "osSockAddr.h"
#include "osTypes.h"
#include "osList.h"
#include "osMBuf.h"
#include "osDebug.h"
#include "osTimer.h"
#include "osMemory.h"

#include "transportIntf.h"
#include "transportConfig.h"
#include "sipAppMain.h"


typedef struct {
	transportAppType_e appType;
	int fd;
	struct sockaddr_in local;		//if local==0, the default local, which was passed in durint thread starts, will be used
	tpLocalSendCallback_h callback;
	uint64_t udpKeepAliveTimerId;	//if the udp is not used (measued by access of this data structure) for KA time, the udp socket will be closed.
	uint32_t accessCount;			//will be cleared each time udpKeepAliveTimer timeout
} tpUdpInfo_t;



static void tpUdpTimeout(uint64_t timerId, void* pData);


static __thread osList_t tpUdpInfoList;


int tpUdpMgmtGetFd(transportAppType_e appType, struct sockaddr_in localAddr)
{
	osListElement_t* pLE = tpUdpInfoList.head;
	while(pLE)
	{
		if(((tpUdpInfo_t*)pLE->data)->appType == appType && osIsSameSA(&((tpUdpInfo_t*)pLE->data)->local, &localAddr)) 
		{
			((tpUdpInfo_t*)pLE->data)->accessCount++;
			return ((tpUdpInfo_t*)pLE->data)->fd;
		}
	
		pLE = pLE->next;
	}

	return -1;
}



tpLocalSendCallback_h tpUdpMgmtGetUdpCallback(int fd)
{
    osListElement_t* pLE = tpUdpInfoList.head;
    while(pLE)
    {
        if(((tpUdpInfo_t*)pLE->data)->fd == fd)
		{
			((tpUdpInfo_t*)pLE->data)->accessCount++;
			return ((tpUdpInfo_t*)pLE->data)->callback;
		}

        pLE = pLE->next;
    }

	return NULL;
}


osStatus_e tpUdpMgmtSetFd(transportAppType_e appType, struct sockaddr_in localAddr, tpLocalSendCallback_h callback, int fd)
{
	tpUdpInfo_t* pUdpInfo = osmalloc(sizeof(tpUdpInfo_t), NULL);
	if(!pUdpInfo)
	{
		logError("fails to osmalloc for pUdpInfo.");
		return OS_ERROR_MEMORY_ALLOC_FAILURE;
	}

	pUdpInfo->appType = appType;
	pUdpInfo->fd = fd;
	pUdpInfo->local = localAddr;
	pUdpInfo->callback = callback;
	pUdpInfo->accessCount = 1;
	pUdpInfo->udpKeepAliveTimerId = osStartTimer(TRANSPORT_UDP_KEEP_ALIVE_TIME, tpUdpTimeout, pUdpInfo);

	osList_append(&tpUdpInfoList, pUdpInfo);

	return OS_STATUS_OK;
}



static void tpUdpTimeout(uint64_t timerId, void* pData)
{
	if(!pData)
	{
		logError("null pointer, pData.");
		return;
	}

	if(((tpUdpInfo_t*)pData)->udpKeepAliveTimerId != timerId)
	{
		logError("timerId(0x%x) does not match with pData->udpKeepAliveTimerId(0x%x), unexpected.", timerId, ((tpUdpInfo_t*)pData)->udpKeepAliveTimerId);
		return;
	}

	if(((tpUdpInfo_t*)pData)->accessCount > 0)
	{
		((tpUdpInfo_t*)pData)->accessCount = 0;
		((tpUdpInfo_t*)pData)->udpKeepAliveTimerId = osStartTimer(TRANSPORT_UDP_KEEP_ALIVE_TIME, tpUdpTimeout, pData);
	}
	else
	{
		osListElement_t* pLE = tpUdpInfoList.head;
		int tpEpFd = appMain_getTpFd();
		struct epoll_event event;
		while(pLE)
		{
			if(pLE->data == pData)
			{
				//close the udp fd
				if(tpEpFd != -1)
				{
        			if(epoll_ctl(tpEpFd, EPOLL_CTL_DEL, ((tpUdpInfo_t*)pData)->fd, &event))
        			{
            			logError("fails to delete file descriptor (%d) to epoll(%d), errno=%d.", ((tpUdpInfo_t*)pData)->fd, tpEpFd, errno);
        			}
				}

        		if(close(((tpUdpInfo_t*)pData)->fd) == -1)
        		{
            		logError("fails to close fd(%d), errno=%d.", ((tpUdpInfo_t*)pData)->fd, errno);
        		}

				osList_unlinkElement(pLE);
				osfree(pLE->data);
				osfree(pLE);
				break;
			}

			pLE = pLE->next;
		}
	}
}


