/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportLib.c
 ********************************************************/


//#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

#include "osDebug.h"
#include "osTimer.h"
#include "osMBuf.h"
#include "osConfig.h"

#include "sipConfig.h"
#include "sipUriparam.h"
#include "sipTransportIntf.h"
#include "tcm.h"
#include "transportLib.h"



osStatus_e tpConvertPLton(transportIpPort_t* pIpPort, bool isIncludePort, struct sockaddr_in* pSockAddr)
{
    osStatus_e status = OS_STATUS_OK;
    char ip[INET_ADDRSTRLEN]={};

    pSockAddr->sin_family = AF_INET;
    if(isIncludePort)
    {
        pSockAddr->sin_port = htons(pIpPort->port);
    }
    else
    {
        pSockAddr->sin_port = 0;
    }

    if(osPL_strcpy(&pIpPort->ip, ip, INET_ADDRSTRLEN) != 0)
    {
        logError("fails to perform osPL_strcpy for IP(%r).", pIpPort->ip);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    if(inet_pton(AF_INET, ip, &pSockAddr->sin_addr.s_addr) != 1)
    {
        logError("fails to perform inet_pton for IP(%s), errno=%d.", ip, errno);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

EXIT:
    return status;
}

osStatus_e tpCreateTcp(int tpEpFd, struct sockaddr_in* peer, struct sockaddr_in* local, int* sockfd, int* connStatus)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if((*sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        logError("could not open a TCP socket, errno=%d.", errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    //do not bind port, use ephemeral port.
    local->sin_port = 0;
    if(bind(*sockfd, (const struct sockaddr *)local, sizeof(struct sockaddr_in)) < 0 )
    {
        logError("fails to bind for sockfd(%d), local(%A), errno=%d", *sockfd, local, errno);
        close(*sockfd);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    *connStatus = connect(*sockfd, (struct sockaddr*)peer, sizeof(struct sockaddr_in));

    if(*connStatus != 0 && errno != EINPROGRESS)
    {
        logError("fails to connect() for sockfd(%d), peer(%A), connStatus=%d, errno=%d.", *sockfd, peer, *connStatus, errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
	}

    struct epoll_event event;
    event.events = EPOLLOUT | EPOLLRDHUP;       //after connection is established, events will be changed to EPOLLIN
    event.data.fd = *sockfd;
    if(epoll_ctl(tpEpFd, EPOLL_CTL_ADD, *sockfd, &event) < 0)
    {
        logError("fails to EPOLL_CTL_ADD sockfd(%d) into epoll fd(%d), errno=%d.", *sockfd, tpEpFd, errno);
        status = OS_ERROR_NETWORK_FAILURE;
        goto EXIT;
    }

    debug("sockfd(%d) is added into epoll fd(%d).", *sockfd, tpEpFd);

EXIT:
	DEBUG_END
    return status;
}


transportAppType_e tpGetAppTypeFromLocalAddr(struct sockaddr_in* pLocal, bool isTcp, osList_t* pTcpAddrList, osList_t* pUdpAddrList)
{
    transportAppType_e appType = TRANSPORT_APP_TYPE_UNKNOWN;

    osListElement_t* pLE = isTcp ? pTcpAddrList->head : pUdpAddrList->head;
    while(pLE)
    {
        if(pLocal->sin_addr.s_addr == ((tpLocalAddrInfo_t*)pLE->data)->local.sin_addr.s_addr)
        {
            appType = ((tpLocalAddrInfo_t*)pLE->data)->appType;
            break;
        }

        pLE = pLE->next;
    }

    return appType;
}
