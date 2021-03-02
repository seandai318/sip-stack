/********************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file cscfHelper.c
 * contains the common helper functions for both SCSCF and ICSCF
 ********************************************************************8***********************/

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "osTypes.h"
#include "osPL.h"
#include "osPrintf.h"
#include "osSockAddr.h"

#include "diaMsg.h"

#include "sipHeader.h"
#include "sipHdrNameValue.h"
#include "sipHdrVia.h"
#include "sipCodecUtil.h"
#include "proxyMgr.h"

#include "scscfRegistrar.h"


osStatus_e cscf_sendRegResponse(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, void* pRegData, uint32_t regExpire, struct sockaddr_in* pPeer, struct sockaddr_in* pLocal, sipResponse_e rspCode)
{
    osStatus_e status = OS_STATUS_OK;

	if(!pReqDecodedRaw || !pPeer || !pLocal)
	{
		logError("null pointer, pReqDecodedRaw=%p, pPeer=%p, pLocal=%p.", pReqDecodedRaw, pPeer, pLocal);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}	

    sipHdrDecoded_t viaHdr={};
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);

    osIpPort_t osPeer;
    osConvertntoPL(pPeer, &osPeer);
    sipHostport_t peer;
    peer.host = osPeer.ip.pl;
    peer.portValue = osPeer.port;

    osMBuf_t* pSipResp = NULL;
    sipHdrName_e sipHdrArray[] = {SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
    int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);

    logInfo("rspCode=%d, local=%A.", rspCode, pLocal);
    switch(rspCode)
    {
        case SIP_RESPONSE_200:
            pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
            status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
            status = sipTU_addContactHdr(pSipResp, pReqDecodedRaw, regExpire);
            status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);

			int len = 0;
			scscfRegInfo_t* pRegInfo = pRegData;
			if(regExpire)
			{
				//add Service-Route
            	sipPointerLen_t sr = SIPPL_INIT(sr);
            	len = osPrintf_buffer((char*)sr.pl.p, SIP_HDR_MAX_SIZE, "Service-Route: <%s;orig;lr>\r\n", SCSCF_URI_WITH_PORT);
            	if(len < 0)
            	{
                	logError("fails to osPrintf_buffer for service-route.");
                	status = OS_ERROR_INVALID_VALUE;
                	goto EXIT;
            	}
            	sr.pl.l = len;

            	osMBuf_writePL(pSipResp, &sr.pl, true);
			
            	//add P-Associated-URI if exists
            	if(pRegInfo)
            	{
                	//for P-Associated-URI
                	sipPointerLen_t pau = SIPPL_INIT(pau);
					for(int i=0; i<pRegInfo->regInfoUENum; i++)
					{
                    	if(!pRegInfo->ueList[i].isImpi)
                    	{
                        	if(!pRegInfo->ueList[i].impuInfo.isBarred)
                        	{
                            	int len = osPrintf_buffer((char*)pau.pl.p, SIP_HDR_MAX_SIZE, "P-Associated-URI: <%r>\r\n", &pRegInfo->ueList[i].impuInfo.impu);
                            	if(len < 0)
                            	{
                                	logError("fails to osPrintf_buffer for service-route.");
                                	status = OS_ERROR_INVALID_VALUE;
                                	goto EXIT;
                            	}
                            	pau.pl.l = len;

                            	osMBuf_writePL(pSipResp, &pau.pl, true);
                        	}
                    	}
					}
                }
			}

            //add P-Charging-Function-Addresses if exists
			if(pRegInfo)
			{
            	sipPointerLen_t chgInfo = SIPPL_INIT(chgInfo);
            	if(pRegInfo->hssChgInfo.chgAddrType != CHG_ADDR_TYPE_INVALID)
            	{
                	len = osPrintf_buffer((char*)chgInfo.pl.p, SIP_HDR_MAX_SIZE, "P-Charging-Function-Addresses: %s=\"%r\"\r\n", pRegInfo->hssChgInfo.chgAddrType == CHG_ADDR_TYPE_CCF ? "ccf" : "ecf", &pRegInfo->hssChgInfo.chgAddr.pl);
                	if(len <0)
                	{
                    	logError("fails to osPrintf_buffer for P-Charging-Function-Addresses.");
                    	status = OS_ERROR_INVALID_VALUE;
                    	goto EXIT;
                	}
                	chgInfo.pl.l = len;
            	}
			}

            status = sipTU_msgBuildEnd(pSipResp, false);
			break;
        case SIP_RESPONSE_INVALID:
            //do nothing here, since pSipResp=NULL, the implementation will be notified to abort the transaction
            goto EXIT;
            break;
        default:
            pSipResp = sipTU_buildUasResponse(pReqDecodedRaw, rspCode, sipHdrArray, arraySize, false);
            status = sipHdrVia_rspEncode(pSipResp, viaHdr.decodedHdr,  pReqDecodedRaw, &peer);
            if(rspCode == SIP_RESPONSE_423)
            {
                status = sipTU_addMsgHdr(pSipResp, SIP_HDR_MIN_EXPIRES, &regExpire, NULL);
            }
            status = sipTU_copySipMsgHdr(pSipResp, pReqDecodedRaw, NULL, 0, true);
            status = sipTU_msgBuildEnd(pSipResp, false);
            break;
    }

    mlogInfo(LM_CSCF, "Response Message=\n%M", pSipResp);

    sipTransMsg_t sipTransMsg = {};

    //fill the peer transport info
    sipHdrViaDecoded_t* pTopVia = ((sipHdrMultiVia_t*)(viaHdr.decodedHdr))->pVia;
    sipHostport_t peerHostPort;
    sipTransport_e peerTpProtocol;
    sipHdrVia_getPeerTransport(pTopVia, &peerHostPort, &peerTpProtocol);

    sipTransMsg.response.sipTrMsgBuf.tpInfo.tpType = peerTpProtocol;
    sipTransMsg.response.sipTrMsgBuf.tpInfo.peer = *pPeer;
    sipTransMsg.response.sipTrMsgBuf.tpInfo.local = *pLocal;

    sipTransMsg.response.sipTrMsgBuf.tpInfo.protocolUpdatePos = 0;

    //fill the other info
    sipTransMsg.sipMsgType = SIP_TRANS_MSG_CONTENT_RESPONSE;
    sipTransMsg.isTpDirect = false;
    sipTransMsg.response.sipTrMsgBuf.sipMsgBuf.pSipMsg = pSipResp;
    sipTransMsg.pTransId = pSipTUMsg->pTransId;
    sipTransMsg.appType = SIPTU_APP_TYPE_CSCF;
    sipTransMsg.response.rspCode = rspCode;
    sipTransMsg.pSenderId = pRegData;
    status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_TU, &sipTransMsg, 0);

    //scscfRegistrar does not need to keep pSipResp, if other layers need it, it is expected they will ref it
    osfree(pSipResp);

EXIT:
    osfree(viaHdr.decodedHdr);
//    osfree(pContactHdr);

	return status;
}


//If the message includes the authorization header, use the username as the IMPI.  Otherwise, use the userhost part of the impu as the impi
osStatus_e cscf_getImpiFromSipMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpu, osPointerLen_t* pImpi)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pReqDecodedRaw || !pImpu || !pImpi)
	{
		logError("null pointer, pReqDecodedRaw=%p, pImpu=%p, pImpi=%p.", pReqDecodedRaw, pImpu, pImpi);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    if(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION])
    {
        sipHdrDecoded_t authHdrDecoded={};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION]->pRawHdr, &authHdrDecoded, true);
        if(status != OS_STATUS_OK)
        {
            logError("fails to decode authorization hdr in sipDecodeHdr.");
            goto EXIT;
        }

        osPointerLen_t username = {"username", sizeof("userName")-1};
        osPointerLen_t* pImpiFromMsg = sipHdrNameValueList_getValue(&((sipHdrMethodParam_t*)authHdrDecoded.decodedHdr)->nvParamList, &username);
		if(!pImpiFromMsg)
		{
			logError("fails to get username from the authorization header.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		osPL_removeQuote(pImpi, pImpiFromMsg, '"');
    }

    //if not pImsi, convert impu to impi
    if(!pImpi->l)
    {
        osPL_shiftcpy(pImpi, pImpu, sizeof("sip:")-1);
    }

EXIT:
	return status;
}


//to make the mapping configurable, to-do
sipResponse_e cscf_cx2SipRspCodeMap(diaResultCode_t diaResultCode)
{
	sipResponse_e sipRspCode = SIP_RESPONSE_200;
	int resultCode = diaResultCode.isResultCode ? diaResultCode.resultCode : diaResultCode.expCode;

	if(resultCode >= 2000 && resultCode < 3000)
		{
			sipRspCode = SIP_RESPONSE_200;
		}
		else
		{
			sipRspCode = SIP_RESPONSE_500;
		}
	
	return sipRspCode;
}


//userNum: in/out, the maximum requested user as the input, the user in the sip request message as the output
osStatus_e cscf_getRequestUser(sipTUMsg_t* pSipTUMsg, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isMO, osPointerLen_t* pUser, int* userNum)
{
	osStatus_e status = OS_STATUS_OK;
	*userNum = 0;

	if(!pSipTUMsg || !pReqDecodedRaw || !pUser)
	{
		logError("null pointer, pSipTUMsg=%p, pReqDecodedRaw=%p, pUser=%p", pSipTUMsg, pReqDecodedRaw, pUser);
		return OS_ERROR_NULL_POINTER;
	}

	/* rules to extract user
     * if exists P-Served-user, and the sessCase matches with isMO, use it, regardless of MO/MT case.  
     * Note this is a deviation from 3GPP24.229, as 24.229 only says to use PSU for MO
     * if P-Served-user DOES NOT exists
     *     For MO, P-Asserted-Identity exists, use it.  PAI may contain 2 entries.   If PAI does not exist, use URI from From header
	 *     for MT, from the req URI
	 */
	//check if there is PSU
    if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER] != NULL)
    {
        sipHdrDecoded_t sipHdrDecoded = {};
        if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER]->pRawHdr, &sipHdrDecoded, false) == OS_STATUS_OK)
		{
        	sipHdrType_pServedUser_t* pPSU = sipHdrDecoded.decodedHdr;
        	osPointerLen_t sescaseName = {"sescase", 7};
        	osPointerLen_t* pSescase = sipParamNV_getValuefromList(&pPSU->genericParam, &sescaseName);
        	bool isSesCaseMO = true;
        	if(pSescase->p[0] == 'o' && osPL_strcmp(pSescase, "orig"))
        	{
            	isSesCaseMO = true;
        	}
        	else if(pSescase->p[0] == 't' && osPL_strcmp(pSescase, "term"))
        	{
            	isSesCaseMO = false;
        	}

        	if(isSesCaseMO == isMO)
        	{
            	pUser[0] = pPSU->uri.sipUser;
	        	*userNum = 1;

				osfree(pPSU);
				goto EXIT;
			}
    
        	osfree(pPSU);        
    	}
		else
		{
			logInfo("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER, use other method to find the sip user.");
		}
	}

	//first check the MT case
    if(!isMO)
    {
        sipFirstline_t firstLine;
        status = sipParser_firstLine(pSipTUMsg->sipMsgBuf.pSipMsg, &firstLine, true);
        if(status == OS_STATUS_OK && !firstLine.isReqLine)
        {
            logError("the sip message is not a request.");
            status = OS_ERROR_INVALID_VALUE;
        }

        if(status != OS_STATUS_OK)
        {
            logError("fails to sipParser_firstLine for MT.");
            goto EXIT;
        }

        pUser[0] = firstLine.u.sipReqLine.sipUri.sipUser;
        *userNum = 1;
        goto EXIT;
    }

	//now checke MO case, first check if there is PAI
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY] != NULL)
	{
		 status = sipDecode_getMGNPHdrURIs(SIP_HDR_P_ASSERTED_IDENTITY, pReqDecodedRaw, pUser, userNum);
		if(status != OS_STATUS_OK)
		{
            logError("fails to sipDecode_getMGNPHdrURIs for SIP_HDR_FROM.");
			*userNum = 0;
			goto EXIT;
		}
	}
	else
	{
		//if there is no PAI, use From header
        status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value, false, pUser);
        if(status == OS_STATUS_OK)
        {
			*userNum = 1;
		}
		else
		{
            logError("fails to sipParamUri_getUriFromRawHdrValue for SIP_HDR_FROM.");
            *userNum = 0;
            goto EXIT;
        }
	}
	
EXIT:
	return status;
}


osStatus_e cscf_getEnumQName(osPointerLen_t* pUser, osPointerLen_t* qName)
{
	osStatus_e status = OS_STATUS_OK;

	//qName shall already allocated space for qName->p
	if(!pUser || !qName || !qName->p)
	{
		logError("null pointer, pUser=%p, qName=%p, qName->p=%p.", pUser, qName, qName->p);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//the pUser may take the form like: sip:+19723247326@ims.com, tel:19723247326, etc.
	//this function would not check the format, simply starts from the first digit, until the last digit to convert to qName
	int digitStart=0;
	int digitStop = 0;
	for(int i=0; i<pUser->l; i++)
	{
		if(!digitStart)
		{
			if(pUser->p[i] <= '9' && pUser->p[i] >= '0')
			{
				digitStart = i;
			}
			else if(pUser->p[i] == '@')
			{
				break;
			}
		}

		if(digitStart && (pUser->p[i] < '0' || pUser->p[i] > '9'))
		{
			digitStop = i;
			break;
		}
	} 

	if(!digitStart)
	{
		logError("the user(%r) is not proper to convert to enum, no digit.", pUser);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	int j=0;
	for (int i=digitStop; i>digitStart; i--)
	{
		((char*)qName->p)[j++] = pUser->p[i];
		((char*)qName->p)[j++] = '.';
	}

	strcpy((char*)&qName->p[j], "e164.arpa");
	qName->l = j + sizeof("e164.arpa") -1;

EXIT:
	return status;
}


sipTuRR_t* cscf_buildOwnRR(osPointerLen_t* pUser, sipTuAddr_t* pOwnAddr)
{
	sipTuRR_t* pOwnRR = oszalloc(sizeof(sipTuRR_t), NULL);

    strcpy((char*)pOwnRR->rawHdr.pl.p, "Record-Route: <sip:");
    pOwnRR->rawHdr.pl.l = sizeof("Record-Route: <sip:") - 1;
	pOwnRR->user.p = &pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l];
	if(pUser)
	{
	    struct timespec tp;
    	clock_gettime(CLOCK_REALTIME, &tp);
    	srand(tp.tv_nsec);
    	int randValue=rand();

		pOwnRR->user.l = snprintf((char*)pOwnRR->rawHdr.pl.p, SIP_HDR_MAX_SIZE, "%lx%x", tp.tv_nsec % tp.tv_sec, randValue);
		pOwnRR->rawHdr.pl.l += pOwnRR->user.l;

		strncpy((char*)&pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l], pUser->p, pUser->l);
        pOwnRR->rawHdr.pl.l += pUser->l;
		pOwnRR->user.l += pUser->l;
		for(int i=0; i<pOwnRR->user.l; i++)
		{
			if((pOwnRR->user.p[i] >='0' && pOwnRR->user.p[i] <=9) ||(pOwnRR->user.p[i] >='A' && pOwnRR->user.p[i] <= 'Z') || (pOwnRR->user.p[i] >= 'a' && pOwnRR->user.p[i] <= 'z'))
			{
				continue;
			}

			//override any other char to be '%'
			((char*)pOwnRR->user.p)[i] = '%';
		}
	}

	osPointerLen_t* pIp = NULL;
	int port = 0;
	if(pOwnAddr->isSockAddr)
	{
		osIpPort_t ipPort;
		osConvertntoPL(&pOwnAddr->sockAddr, &ipPort);
		pIp = &ipPort.ip.pl;
		port = ipPort.port;
	}
	else
	{
		pIp = &pOwnAddr->ipPort.ip;
		port = pOwnAddr->ipPort.port;
	}

	if(port)
	{
		if(pOwnAddr->tpType == TRANSPORT_TYPE_ANY)
		{
			pOwnRR->rawHdr.pl.l += osPrintf_buffer((char*)&pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l], SIP_HDR_MAX_SIZE, "@%r:%d; lr>\r\n", pIp, port);
		}
		else
		{
			pOwnRR->rawHdr.pl.l += osPrintf_buffer((char*)&pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l], SIP_HDR_MAX_SIZE, "@%r:%d; transport=%s; lr>\r\n",  pIp, port, pOwnAddr->tpType == TRANSPORT_STATUS_UDP ? "udp" : "tcp");
		}
	}
	else
	{
        if(pOwnAddr->tpType == TRANSPORT_TYPE_ANY)
        {
			pOwnRR->rawHdr.pl.l += osPrintf_buffer((char*)&pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l], SIP_HDR_MAX_SIZE, "@%r; lr>\r\n", pIp);
		}
		else
		{
            pOwnRR->rawHdr.pl.l += osPrintf_buffer((char*)&pOwnRR->rawHdr.pl.p[pOwnRR->rawHdr.pl.l], SIP_HDR_MAX_SIZE, "@%r; transport=%s; lr>\r\n",  pIp, pOwnAddr->tpType == TRANSPORT_STATUS_UDP ? "udp" : "tcp");
		}
	}

EXIT:
	return pOwnRR;
}		
