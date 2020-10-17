/********************************************************
 * Copyright (C) 2019, Sean Dai
 *
 * @file sipClient.c 
 ********************************************************/

#include <string.h>
#include <stdio.h>
#include "osMBuf.h"
#include "osDebug.h"

#include "sipHeader.h"
#include "sipConfig.h"
#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipHdrRoute.h"
#include "sipHdrNameValue.h"
#include "sipHdrVia.h"
#include "sipHdrFromto.h"
#include "sipHdrMisc.h"


sipMsgRequest_t* sipClientBuildReq(sipRequest_e reqType, char* reqUri, char* fromUri, char* toUri, char* callId, uint32_t cseq)
{
	osStatus_e status = OS_STATUS_OK;
	sipUri_t sipReqUri={};
	sipMsgRequest_t* pReq = NULL;
	sipHdrAddCtrl_t ctrl = {false, true, false, NULL};

	if(reqUri)
	{
		sipReqUri.sipUser.p = reqUri;
		sipReqUri.sipUser.l = strlen(reqUri);
	}
	else
	{
		sipReqUri.sipUriType = URI_TYPE_SIP;
		sipReqUri.userInfo.sipUser.user.p="interLogic-user";
		sipReqUri.userInfo.sipUser.user.l=15;
		sipReqUri.userInfo.sipUser.isTelSub = false;
		sipReqUri.userInfo.password.l=0;
		sipReqUri.hostport.host.p="10.11.12.13";
		sipReqUri.hostport.host.l=11;
		sipReqUri.hostport.portValue = 5060;
	}
#if 0
	sipReqUri.uriParam.uriParamMask = 0;
	sipReqUri.uriParam.transport.l=0;
	sipReqUri.uriParam.user.l=0;
	sipReqUri.uriParam.method.l=0;
	sipReqUri.uriParam.ttl.l=0;
	sipReqUri.uriParam.maddr.l=0;
	osList_init(&sipReqUri.uriParam.other);
#endif

	pReq = sipMsgCreateReq(reqType, &sipReqUri);
	if(!pReq)
	{
		logError("fails to sipMsgCreateReq() for reqType (%d).", reqType);
		goto EXIT;
	}

	//add Via
	sipHdrVia_t viaHdr={};
    viaHdr.sentProtocol[2].p = "UDP";
    viaHdr.sentProtocol[2].l = 3;
    sipConfig_getHost(&viaHdr.hostport.host, &viaHdr.hostport.portValue);
    status = sipHdrVia_generateBranchId(&pReq->viaBranchId, "test");
	if(status != OS_STATUS_OK)
	{
		logError("fails to generate via branch id.");
		goto EXIT;
	}
    sipHdrParamNameValue_t branch={{"branch", 6}, pReq->viaBranchId};
	viaHdr.pBranch = &branch;
    sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
    sipHdrParamNameValue_t param2={{"sigcomp-id", 10}, {"\"urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8\"", strlen("\"urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8\"")}};
    osList_append(&viaHdr.viaParamList, &param1);
    osList_append(&viaHdr.viaParamList, &param2);
    sipMsgAddHdr(pReq->sipRequest, SIP_HDR_VIA, &viaHdr, NULL, ctrl);
	osList_clear(&viaHdr.viaParamList);

	//add from
    sipUriExt_t sipFromUri={};
	if(fromUri)
	{
        sipFromUri.uri.sipUser.p=fromUri;
        sipReqUri.sipUser.l=strlen(fromUri);
	}
	else
	{
    	sipFromUri.displayName.p="sean";
    	sipFromUri.displayName.l=4;
		status = sipParamUri_build(&sipFromUri.uri, URI_TYPE_SIP, "sean", 4, NULL, 0, "interlogic.com", 14, 5060);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to sipParamUri_build for From Hdr.");
        	goto EXIT;
    	}
	}
	osPointerLen_t transport={"udp", 3};
	osPointerLen_t lr={"lr", 2};
	osPointerLen_t dn = {"sean", 4};
	sipParamUri_addDisplayName(&sipFromUri, &dn); 
	sipParamUri_addParam(&sipFromUri.uri, SIP_URI_PARAM_TRANSPORT, &transport);
	sipParamUri_addParam(&sipFromUri.uri, SIP_URI_PARAM_LR, &lr);
	sipHdrParamNameValue_t uriOther={{"test", 4}, {"testValue", 9}};
    sipParamUri_addParam(&sipFromUri.uri, SIP_URI_PARAM_OTHER, &uriOther);
    status = sipHdrFromto_generateTagId(&pReq->fromTag, false);
    if(status != OS_STATUS_OK)
    {
        logError("fail to generate from tagId.");
        goto EXIT;
    }
	
    sipMsgAddHdr(pReq->sipRequest, SIP_HDR_FROM, &sipFromUri, &pReq->fromTag, ctrl);

	//add To
    sipUriExt_t sipToUri={};
    if(toUri)
    {
        sipToUri.uri.sipUser.p=toUri;
        sipReqUri.sipUser.l=strlen(toUri);
    }
    else
    {
        sipToUri.displayName.p="sean-user";
        sipToUri.displayName.l=9;
        sipParamUri_build(&sipToUri.uri, URI_TYPE_SIP, "sean", 4, NULL, 0, "interlogic.com", 14, 5060);
    }
#if 0	
    sipToUri.uri.uriParam.uriParamMask = 0;
    sipToUri.uri.uriParam.transport.l=0;
    sipToUri.uri.uriParam.user.l=0;
    sipToUri.uri.uriParam.method.l=0;
    sipToUri.uri.uriParam.ttl.l=0;
    sipToUri.uri.uriParam.maddr.l=0;
    osList_init(&sipToUri.uri.uriParam.other);
#endif
	sipMsgAddHdr(pReq->sipRequest, SIP_HDR_TO, &sipToUri, NULL, ctrl);

	//callid
	sipRequest_e hdrCode = SIP_HDR_CALL_ID;
    char* callIdValue = osmalloc(SIP_MAX_CALL_ID_LEN, NULL);
    if(callIdValue == NULL)
    {
       	logError("allocate callId memory fails.");
       	status = OS_ERROR_NULL_POINTER;
       	goto EXIT;
    }
	pReq->callId.p = callIdValue;

    if(!callId)
    {
		status = sipHdrCallId_createCallId(&pReq->callId);
		if(status != OS_STATUS_OK)
		{
			logError("fails to generate call id.");
			goto EXIT;
		}
    }
    else
    {
	    char* callIdValue = osmalloc(SIP_MAX_CALL_ID_LEN, NULL);
    	if(callIdValue == NULL)
    	{
        	logError("allocate callId memory fails.");
        	status = OS_ERROR_NULL_POINTER;
        	goto EXIT;
    	}
        pReq->callId.l = sprintf(callIdValue, "%s", callId);
        pReq->callId.p = callIdValue;
    }
	sipMsgAddHdr(pReq->sipRequest, SIP_HDR_CALL_ID, &pReq->callId, &hdrCode, ctrl);

	//cseq
	sipMsgAddHdr(pReq->sipRequest, SIP_HDR_CSEQ, &cseq, &reqType, ctrl);

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pReq);
		pReq = NULL;
	}

	return pReq;	
}


#if 0
//Route: <sip:10.211.138.196:5060>;lr
	sipHdrRouteElementPT_t route;
	sipUri_t uri;
    sipParamUri_build(&uri, URI_TYPE_SIP, "seanR", 5, NULL, 0, "interlogic.com", 14, 5060);
	osPointerLen_t rdn = {"route", 5};
	sipHdrRoute_build(&route, &uri, &rdn);
	sipMsgAddHdr(pSipMsg, SIP_HDR_ROUTE, &route, false);

	//contact
	sipParamUri_build(&uri, URI_TYPE_SIP, "310970200000008", 15, NULL, 0, "10.193.0.10", 11, 5060);
	sipHdrParamNameValue_t otherparam={{"comp", 4}, {"sigcomp", 7}};
	sipParamUri_addParam(&uri, SIP_URI_PARAM_OTHER, &otherparam);
	sipHdrParamNameValue_t otherparam1={{"sigcomp-id", 10}, {"urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8", strlen("urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8")}};
	sipParamUri_addParam(&uri, SIP_URI_PARAM_OTHER, &otherparam1);
	sipHdrGenericNameParamPt_t contact;
	sipHdrContact_build(&contact, &uri, NULL);
	sipHdrParamNameValue_t otherparam2={{"+sip.instance", strlen("+sip.instance")},{"\"<urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8>\"", strlen("\"<urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8>\"")}};
	sipHdrContact_addParam(&contact, &otherparam2);
    sipHdrParamNameValue_t otherparam3={{"+g.3gpp.icsi-ref", strlen("+g.3gpp.icsi-ref")}, {"\"urn%3Aurn-7%3A3gpp-service.ims.icsi.mmtel\"", strlen("\"urn%3Aurn-7%3A3gpp-service.ims.icsi.mmtel\"")}};
    sipHdrContact_addParam(&contact, &otherparam3);
    sipMsgAddHdr(pSipMsg, SIP_HDR_CONTACT, &contact, false);
		
	sipHdr_nameValue_t pani;
	sipHdrNameValue_build(&pani);
    sipParamNameValue_t nv1={{"3GPP-UTRAN-FDD", 14}, {NULL, 0}};
	sipParamNameValue_t nv2={{"utran-cell-id-3gpp", strlen("utran-cell-id-3gpp")}, {"\"31097000010012710\"", strlen("\"31097000010012710\"")}};
	sipParamNameValue_t nv3={{"operator-specific-GI", strlen("operator-specific-GI")}, {"\"31.7983 -97.6116 33.0000\"", strlen("\"31.7983 -97.6116 33.0000\"")}};
	sipParamNameValue_t nv4={{"local-time-zone", strlen("local-time-zone")}, {"\"UTC-00:00\"", strlen("\"UTC-00:00\"")}};
	sipHdrNameValue_addParam(&pani, &nv1);
	sipHdrNameValue_addParam(&pani, &nv2);
    sipHdrNameValue_addParam(&pani, &nv3);
    sipHdrNameValue_addParam(&pani, &nv4);
	sipMsgAddHdr(pSipMsg, SIP_HDR_P_ACCESS_NETWORK_INFO, &pani, false);

	sipHdr_name_t allow;
	sipHdrName_build(&allow);
	sipParamName_t n1={"INVITE,ACK,BYE,CANCEL,REFER,NOTIFY,OPTIONS,PRACK,UPDATE,INFO,MESSAGE,SUBSCRIBE", strlen("INVITE,ACK,BYE,CANCEL,REFER,NOTIFY,OPTIONS,PRACK,UPDATE,INFO,MESSAGE,SUBSCRIBE")};
	sipParamName_t n2={"PUBLISH", 7};
	sipHdrName_addParam(&allow, &n1);
	sipHdrName_addParam(&allow, &n2);
    sipMsgAddHdr(pSipMsg, SIP_HDR_ALLOW, &allow, false);

	sipHdr_name_t supported;
	sipHdrName_build(&supported);
	osPL_setStr(&n1,"replaces", 8);
	osPL_setStr(&n2, "timer, gruu, join", strlen("timer, gruu, join"));
    sipHdrName_addParam(&supported, &n1);
    sipHdrName_addParam(&supported, &n2);
    sipMsgAddHdr(pSipMsg, SIP_HDR_SUPPORTED, &supported, false);

	sipHdr_nameValue_t ac;
	sipHdrNameValue_build(&ac);
	sipParamNameValue_t nv11={{"*", 1}, {NULL, 0}};
	sipParamNameValue_t nv22={{"+g.3gpp.icsi-ref", strlen("+g.3gpp.icsi-ref")}, {"\"urn%3Aurn-7%3A3gpp-service.ims.icsi.mmtel\"", strlen("\"urn%3Aurn-7%3A3gpp-service.ims.icsi.mmtel\"")}};
    sipHdrNameValue_addParam(&ac, &nv11);
	sipHdrNameValue_addParam(&ac, &nv22);
    sipMsgAddHdr(pSipMsg, SIP_HDR_ACCEPT_CONTACT, &ac, false);
	

	printf("sipMsg.pos=%ld\n", pSipMsg->sipRequest->pos);
	osPointerLen_t pl;
	pl.p = pSipMsg->sipRequest->buf;
	pl.l = pSipMsg->sipRequest->pos;
	debug("sipMsg=\n%r", &pl);

	debug("reqType=%d", pSipMsg->reqType);
	debug("viaBranchId=%r", &pSipMsg->viaBranchId);
	debug("fromTag=%r", &pSipMsg->fromTag);
	debug("toTag=%r", &pSipMsg->toTag);
	debug("callId=%r", &pSipMsg->callId);
	return 0;
}

#endif
	
