/********************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file cscfHelper.c
 * contains the common helper functions for both SCSCF and ICSCF
 ********************************************************************8***********************/

#include "osTypes.h"
#include "osPL.h"
#include "osPrintf.h"

#include "diaMsg.h"

#include "sipHeader.h"
#include "sipHdrNameValue.h"
#include "sipHdrVia.h"

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
