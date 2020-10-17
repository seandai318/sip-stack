/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file transportMgr.c
 ********************************************************/


#include <netinet/in.h>
#include <arpa/inet.h>

#include "osConfig.h"
#include "osDebug.h"
#include "osSockAddr.h"

#include "sipConfig.h"

#include "sipTransportIntf.h"
#include "transportCom.h"
#include "sipAppMain.h"
#include "transportConfig.h"
#include "tcm.h"



transportStatus_e transport_send(transportAppType_e appType, void* appId, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd)
{
    logInfo("send a message via appType=%d, tpType=%d, tcpFd=%d, isCom=%d, peer=%A, local=%A.", appType, pTpInfo->tpType, pTpInfo->tcpFd, pTpInfo->isCom, &pTpInfo->peer, &pTpInfo->local);
    if(pTpInfo->tpType == TRANSPORT_TYPE_ANY)
    {
		osIpPort_t osPeer;
		osConvertntoPL(&pTpInfo->peer, &osPeer);
		pTpInfo->tpType = tpConfig_getTransport(appType, &osPeer.ip.pl, osPeer.port, pBuf->size);
		if(pTpInfo->tpType = TRANSPORT_TYPE_ANY)
		{
			pTpInfo->tpType == TRANSPORT_STATUS_UDP;
		}
	}
			
    if(pTpInfo->isCom)
    {
        return com_send(appType, appId, pTpInfo, pBuf, fd);
    }
    else
    {
		logError("unexpected isCom = FALSE.  For appType(%d), message has to be sent via COM.", appType);
		return TRANSPORT_STATUS_FAIL;
    }
}



sipTransportStatus_e sipTransport_send(void* pTrId, transportInfo_t* pTpInfo, osMBuf_t* pSipBuf)
{
	logInfo("send a sip message via tpType=%d, tcpFd=%d, isCom=%d, peer=%A.", pTpInfo->tpType, pTpInfo->tcpFd, pTpInfo->isCom, &pTpInfo->peer);
	//for TCP, only send via com if tcpFd != -1
	if(pTpInfo->tpType == TRANSPORT_TYPE_TCP && pTpInfo->tcpFd != -1)
	{
		if(pTpInfo->isCom)
		{
			return com_send(TRANSPORT_APP_TYPE_SIP, pTrId, pTpInfo, pSipBuf, NULL);
		}
		else
		{
			return sipTpClient_send(pTrId, pTpInfo, pSipBuf);
		}
	}

    //check which transport protocol to use
    bool isUDP = true;
    osIpPort_t osPeer;
    osConvertntoPL(&pTpInfo->peer, &osPeer);
	switch(sipConfig_getTransport(&osPeer.ip.pl, osPeer.port))
    //switch(sipConfig_getTransport(&pTpInfo->peer.ip, pTpInfo->peer.port))
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

logError("to-remove, isUDP=%d", isUDP);

    //check which transport protocol to use
	if(isUDP)
	{
		pTpInfo->tpType = TRANSPORT_TYPE_UDP;
		pTpInfo->tcpFd = -1;

		//ims client has a defect that when sending response, it does not honer sent-by in the top via, always send via the real request sending ip:port
		if(SIP_CONFIG_USE_IMS_CLIENT)
		{
			return com_send(TRANSPORT_APP_TYPE_SIP, pTrId, pTpInfo, pSipBuf, NULL);
		}
	}
	else
	{
		pTpInfo->tpType = TRANSPORT_TYPE_TCP;
	}

	return sipTpClient_send(pTrId, pTpInfo, pSipBuf);
}


void sipTransportMsgBuf_free(void* pData)
{
	sipTransportMsgBuf_t* pMsgBuf = pData;

	osMBuf_dealloc(pMsgBuf->pSipBuf);
}


osStatus_e transport_closeTcpConn(int tcpFd, bool isCom)
{
	osStatus_e status = OS_STATUS_OK;

	if(!isCom)
	{
		logError("do not support stop conn in app thread.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	int epFd = -1;
	if(isCom)
	{
		epFd = com_getTpFd();
	}
	else
	{
		epFd = appMain_getTpFd();
	}

	status = tpTcmCloseTcpConn(epFd, tcpFd, false);

EXIT:
	return status;
}

void transport_localRegApp(transportAppType_e appType, tpLocalSendCallback_h callback)
{
    return tpLocal_appReg(appType, callback);
}


//app requires send a udp message, a udp socket will be opened per appType if it has not been opened.  when receiving a response, callback(0 will be called
transportStatus_e transport_localSend(transportAppType_e appType, transportInfo_t* pTpInfo, osMBuf_t* pBuf, int* fd)
{
    if(pTpInfo->tpType != TRANSPORT_TYPE_UDP || pTpInfo->isCom)
    {
        logError("pTpInfo->tpType(%d) != TRANSPORT_TYPE_UDP or pTpInfo->isCom(%d) is true.", pTpInfo->tpType, pTpInfo->isCom);
        return TRANSPORT_STATUS_FAIL;
    }

    return tpLocal_udpSend(appType, pTpInfo, pBuf, fd);
}

		
