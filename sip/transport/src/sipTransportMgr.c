#include "osConfig.h"
#include "osDebug.h"

#include "sipConfig.h"

#include "sipTransportIntf.h"
#include "sipTransportServer.h"
#include "sipTransportClient.h"



sipTransportStatus_e sipTransport_send(void* pTrId, sipTransportInfo_t* pTpInfo, osMBuf_t* pSipBuf)
{
	logInfo("send a sip message via tpType=%d, tcpFd=%d, isServer=%d, peer=%r:%d.", pTpInfo->tpType, pTpInfo->tcpFd, pTpInfo->isServer, &pTpInfo->peer.ip, pTpInfo->peer.port);
	if(pTpInfo->tpType == SIP_TRANSPORT_TYPE_TCP && pTpInfo->tcpFd != -1)
	{
		if(pTpInfo->isServer)
		{
			return sipTpServer_send(pTpInfo, pSipBuf);
		}
		else
		{
			return sipTpClient_send(pTrId, pTpInfo, pSipBuf);
		}
	}

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

logError("to-remove, isUDP=%d", isUDP);

    //check which transport protocol to use
	if(isUDP)
	{
		pTpInfo->tpType = SIP_TRANSPORT_TYPE_UDP;
		pTpInfo->tcpFd = -1;

		//ims client has a defect that when sending response, it does not honer sent-by in the top via, always send via the real request sending ip:port
		if(SIP_CONFIG_USE_IMS_CLIENT)
		{
			return sipTpServer_send(pTpInfo, pSipBuf);
		}
	}
	else
	{
		pTpInfo->tpType = SIP_TRANSPORT_TYPE_TCP;
	}

	return sipTpClient_send(pTrId, pTpInfo, pSipBuf);
}


void sipTransportMsgBuf_free(void* pData)
{
	sipTransportMsgBuf_t* pMsgBuf = pData;

	osMBuf_dealloc(pMsgBuf->pSipBuf);
}

