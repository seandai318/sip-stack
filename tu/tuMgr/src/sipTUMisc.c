/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUMisc.c
 ********************************************************/


#include "osTypes.h"
#include "osDebug.h"
#include "osSockAddr.h"
#include "osPL.h"

#include "sipConfig.h"
#include "sipTransIntf.h"
#include "transportIntf.h"
#include "sipHdrVia.h"
#include "sipTU.h"
#include "sipTUMisc.h"


static osStatus_e sipTu_convertPL2NextHop(osPointerLen_t* pUri, transportIpPort_t* pNextHop);


void* sipTU_sendReq2Tr(sipRequest_e nameCode, osMBuf_t* pReq, sipTransViaInfo_t* pViaId, sipTuAddr_t* nextHop, bool isTpDirect, sipTuAppType_e appType, size_t topViaProtocolPos, void* pTuInfo)
{
	osStatus_e status = OS_STATUS_OK;
    sipTransInfo_t sipTransInfo;
	void* pTransId = NULL;

	sipTransInfo.isRequest = true;
	sipTransInfo.transId.viaId = *pViaId;
    sipTransInfo.transId.reqCode = nameCode;

    sipTransMsg_t sipTransMsg = {};
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_REQUEST;
	sipTransMsg.isTpDirect = isTpDirect;
	sipTransMsg.appType = appType;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.pSipMsg = pReq;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.reqCode = nameCode;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.isRequest = true;
    sipTransMsg.request.sipTrMsgBuf.sipMsgBuf.hdrStartPos = 0;
    sipTransMsg.request.pTransInfo = &sipTransInfo;

	if(!nextHop->isSockAddr)
	{
		osIpPort_t ipPort = {{nextHop->ipPort.ip, false, false}, nextHop->ipPort.port};
		osConvertPLton(&ipPort, true, &sipTransMsg.request.sipTrMsgBuf.tpInfo.peer);
	}
	else
	{
		sipTransMsg.request.sipTrMsgBuf.tpInfo.peer = nextHop->sockAddr;
	}

	sipConfig_getHost1(&sipTransMsg.request.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.request.sipTrMsgBuf.tpInfo.protocolUpdatePos = topViaProtocolPos;
    sipTransMsg.pTransId = NULL;
    sipTransMsg.pSenderId = pTuInfo;

    if(sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0) == OS_STATUS_OK)
	{
		pTransId = sipTransMsg.pTransId;
	}
	
	return pTransId;
}


//peerViaIdx indicates from which via to get the peer address, if peerViaIdx=SIP_VIA_IDX_MAX, the pointed via is the bottom via
osStatus_e sipTU_sendRsp2Tr(sipResponse_e rspCode, osMBuf_t* pResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, void* pTransId, void* pAppId)
{
	osStatus_e status = OS_STATUS_OK;
    sipTransMsg_t sipTransMsg = {};

    //fill the peer transport info
    sipHostport_t peerHostPort;
    sipTransport_e peerTpProtocol;
  	sipHdrVia_getPeerTransportFromRaw(pReqDecodedRaw, peerViaIdx, &peerHostPort, &peerTpProtocol);

    sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
	osIpPort_t ipPort = {{peerHostPort.host, false, false}, peerHostPort.portValue};
	osConvertPLton(&ipPort, true, &sipTransMsg.response.sipTrMsgBuf.tpInfo.peer);
	sipConfig_getHost1(&sipTransMsg.response.sipTrMsgBuf.tpInfo.local);
    sipTransMsg.response.sipTrMsgBuf.tpInfo.protocolUpdatePos = 0;

    //fill the other info
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
	if(!pTransId)
	{
		sipTransMsg.isTpDirect = true;
	}
    sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pResp;
    sipTransMsg.pTransId = pTransId;
    sipTransMsg.response.rspCode = rspCode;
    sipTransMsg.pSenderId = pAppId;

    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

EXIT:
	return status;
}


//if pRegTimeConfig == NULL, do not check max/min expire time and no need to fill default time
//return value: isExpireFound
bool sipTu_getRegExpireFromMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t* pRegExpire, sipTuRegTimeConfig_t* pRegTimeConfig, sipResponse_e* pRspCode)
{
	osStatus_e status = OS_STATUS_OK;
	bool isExpireFound = false;

	sipHdrDecoded_t* pContactHdr = NULL;
    *pRspCode = SIP_RESPONSE_INVALID;

    //check the expire header
	osPointerLen_t* pContactExpire = NULL;
    if(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES] != NULL)
    {
        sipHdrDecoded_t expiryHdr;
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES]->pRawHdr, &expiryHdr, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to get expires value from expires hdr by sipDecodeHdr.");
            *pRspCode = SIP_RESPONSE_400;
            goto EXIT;
        }

        *pRegExpire = *(uint32_t*)expiryHdr.decodedHdr;
        osfree(expiryHdr.decodedHdr);
        isExpireFound = true;
    }
    else
    {
        pContactHdr = oszalloc(sizeof(sipHdrDecoded_t), sipHdrDecoded_cleanup);
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, pContactHdr, true);
        if(status != OS_STATUS_OK)
        {
            logError("fails to decode contact hdr in sipDecodeHdr.");
            *pRspCode = SIP_RESPONSE_400;
            goto EXIT;
        }

        osPointerLen_t expireName={"expires", 7};
        //pContactExpire is not allocated an new memory, it just refer to a already allocated memory in pGNP->hdrValue, no need to dealloc memory for pContactExpire
        pContactExpire = sipHdrGenericNameParam_getGPValue(&((sipHdrMultiContact_t*)pContactHdr->decodedHdr)->contactList.pGNP->hdrValue, &expireName);
        if(pContactExpire != NULL)
        {
            isExpireFound = true;
            *pRegExpire = osPL_str2u32(pContactExpire);
        }
    }

	if(!pRegTimeConfig)
	{
		goto EXIT;
	}

    if(!isExpireFound)
    {
        *pRegExpire = pRegTimeConfig->defaultRegTime;
    }

    if(*pRegExpire != 0 && *pRegExpire < pRegTimeConfig->minRegTime)
    {
        *pRegExpire = pRegTimeConfig->minRegTime;
        *pRspCode = SIP_RESPONSE_423;
        goto EXIT;
    }
    else if (*pRegExpire > pRegTimeConfig->maxRegTime)
    {
        *pRegExpire = pRegTimeConfig->maxRegTime;
        if(pContactExpire)
        {
            osPL_modifyu32(pContactExpire, *pRegExpire);
        }
    }

EXIT:
	osfree(pContactHdr);

    return isExpireFound;
}


osStatus_e sipTu_convertUri2NextHop(sipTuUri_t* pUri, sipTuNextHop_t* pNextHop)
{
	osStatus_e status = OS_STATUS_OK;

	if(pUri->isRaw)
	{
		pNextHop->nextHopRaw = pUri->rawSipUri;
		status = sipTu_convertPL2NextHop(&pUri->rawSipUri, &pNextHop->nextHop.ipPort);
	}
	else
	{
		pNextHop->nextHopRaw = pUri->sipUri.sipUser;
		status = sipTu_convertPL2NextHop(&pUri->sipUri.hostport.host, &pNextHop->nextHop.ipPort);
		pNextHop->nextHop.ipPort.port = pUri->sipUri.hostport.portValue;
	}

	return status;
}


typedef enum {
	SIP_TU_URI_SERACH_STATE_NONE,	//beginning of a search
	SIP_TU_URI_SERACH_STATE_USER,	//searching for user
	SIP_TU_URI_SERACH_STATE_HOST,	//searching for host
	SIP_TU_URI_SERACH_STATE_PORT,	//searching for port
} sipTuUriSearchState_e;


//<sip:123.com>, <sip:123.com:5060>, <sip:user@123.com>, <sip:user@123.com:5060>, sip:123.com, sip:123.com:5060, sip:user@123.com, sip:user@123.com:5060, sip:123.com;lr, etc.
static osStatus_e sipTu_convertPL2NextHop(osPointerLen_t* pUri, transportIpPort_t* pNextHop)
{
	osStatus_e status = OS_STATUS_OK;

	size_t startPos = 0;
	sipTuUriSearchState_e sipTuUriSearchState = SIP_TU_URI_SERACH_STATE_NONE;
	size_t pos = 0;
	for(pos = 0; pos < pUri->l; pos++)
	{
		if(pUri->p[pos] != '@' && pUri->p[pos] != ';' && pUri->p[pos] != '>' && pUri->p[pos] != ':')
		{
			continue;
		}

		switch(sipTuUriSearchState)
		{
			case SIP_TU_URI_SERACH_STATE_NONE:
				if(pUri->p[pos] == ':')
				{
					startPos = pos+1;
					sipTuUriSearchState = SIP_TU_URI_SERACH_STATE_USER;
				}
				else
				{
					status = OS_ERROR_INVALID_VALUE;
					goto EXIT;
				}
				break;
			case SIP_TU_URI_SERACH_STATE_USER:
				if(pUri->p[pos] == '@')
				{
					sipTuUriSearchState = SIP_TU_URI_SERACH_STATE_HOST;
					startPos = pos+1;
				}
				else if(pUri->p[pos] == ':')
				{
                    pNextHop->ip.p = &pUri->p[startPos];
                    pNextHop->ip.l = pos - startPos;
                    startPos = pos+1;

					sipTuUriSearchState = SIP_TU_URI_SERACH_STATE_HOST;
				}
				else	//case '>' or ';'
				{
                    pNextHop->ip.p = &pUri->p[startPos];
                    pNextHop->ip.l = pos - startPos;
					pNextHop->port = 0;
					goto EXIT;
				}							
				break;
			case SIP_TU_URI_SERACH_STATE_HOST:
				if(pUri->p[pos] == '@')
				{
					status = OS_ERROR_INVALID_VALUE;
					goto EXIT;
				}
				else if(pUri->p[pos] == ':')
				{
   	                pNextHop->ip.p = &pUri->p[startPos];
                    pNextHop->ip.l = pos - startPos;
					startPos = pos+1;

                    sipTuUriSearchState = SIP_TU_URI_SERACH_STATE_PORT;
               	}
               	else 	//case '>' or ';'
                {
                    pNextHop->ip.p = &pUri->p[startPos];
                    pNextHop->ip.l = pos - startPos;

                   	pNextHop->port = 0;
                   	goto EXIT;
                }
				break;
			case SIP_TU_URI_SERACH_STATE_PORT:
				if(pUri->p[pos] == '@' || pUri->p[pos] == ':')
				{
					status = OS_ERROR_INVALID_VALUE;
					goto EXIT;
				}
				else //case '>' or ';'
				{
					osPointerLen_t portStr = {&pUri->p[startPos], pos - startPos};
					osPL_compact(&portStr);
					pNextHop->port = osPL_str2u32(&portStr);
				}
				break;
			default:
				break;
		}
	}

	switch(sipTuUriSearchState)
	{
		case SIP_TU_URI_SERACH_STATE_USER:
		case SIP_TU_URI_SERACH_STATE_HOST:
			pNextHop->ip.p = &pUri->p[startPos];
			pNextHop->ip.l = pos - startPos;
			pNextHop->port = 0;
			break;
		case SIP_TU_URI_SERACH_STATE_PORT:
		{
			osPointerLen_t portStr = {&pUri->p[startPos], pos - startPos};
			osPL_compact(&portStr);
			pNextHop->port = osPL_str2u32(&portStr);
		}
		default:
			break;
	}

	osPL_compact(&pNextHop->ip);

EXIT:
	return status;
}


void sipTUMsg_cleanup(void* data)
{
	while(!data)
	{
		return;
	}

	sipTUMsg_t* pTuMsg = data;
	if(pTuMsg->sipTuMsgType == SIP_TU_MSG_TYPE_MESSAGE)
	{
		osfree(pTuMsg->sipMsgBuf.pSipMsg);
	}
}


void sipTU_mapTrTuId(void* pTrId, void* pTuId)
{
	sipTr_setTuId(pTrId, pTuId);
}

sipTuRR_t* sipTU_createRR()
{
	sipTuRR_t* pOwnRR = oszalloc(sizeof(sipTuRR_t), NULL);

	pOwnRR->rawHdr.pl.p = pOwnRR->rawHdr.buf;
	pOwnRR->rawHdr.pl.l = 0;

	return pOwnRR;
} 	
