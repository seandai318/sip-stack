/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipProxy.c
 ********************************************************/

#include <string.h>

#include "osDebug.h"
#include "osMBuf.h"
#include "osList.h"

#include "sipConfig.h"
#include "sipMsgRequest.h"
#include "sipHeader.h"
#include "sipHdrRoute.h"
#include "sipHdrVia.h"
#include "sipHdrMisc.h"


sipMsgRequest_t* sipProxyBuildReq(sipMsgDecoded_t* sipMsgInDecoded)
{
	osStatus_e status = OS_STATUS_OK;
	sipMsgRequest_t* pReq = NULL;

	if(!sipMsgInDecoded)
	{
		logError("null pointer, sipMsgInDecoded.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    sipHdrNmT_t nmt[] = {{SIP_HDR_VIA, SIP_HDR_MODIFY_TYPE_ADD}, {SIP_HDR_VIA, SIP_HDR_MODIFY_TYPE_REPLACE}, {SIP_HDR_MAX_FORWARDS, SIP_HDR_MODIFY_TYPE_REPLACE}, {SIP_HDR_RECORD_ROUTE, SIP_HDR_MODIFY_TYPE_REMOVE}};

    pReq = sipMsgCreateProxyReq(sipMsgInDecoded, nmt, sizeof(nmt)/sizeof(nmt[0]));
	if(!pReq)
	{
		logError("fails to create sipMsgRequest_t for proxy.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

EXIT:
	return pReq;
}


osStatus_e sipProxyHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgRequest_t* pReqMsg, sipHdrNmT_t* pNMT)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;
	sipHdrAddCtrl_t ctrl = {false, false, false, NULL};

    if(!sipMsgInDecoded || !pReqMsg  || ! pNMT)
    {
        logError("null pointer, sipMsgInDecoded=%p, pReqMsg=%p, pNMT=%p.", sipMsgInDecoded, pReqMsg, pNMT);
        status = OS_ERROR_NULL_POINTER;
		goto EXIT;
    }

	debug("to remove, hdrCode=%d, modType=%d", pNMT->hdrCode, pNMT->modType);
	sipHdrEncode_h sipHdrEncode = sipHdrGetEncode(pNMT->hdrCode);

	switch(pNMT->hdrCode)
	{
		case SIP_HDR_MAX_FORWARDS:
		{
	        sipHdrDecoded_u hdrDecoded;
    	    status = sipHdrGetValue(sipMsgInDecoded, SIP_HDR_MAX_FORWARDS, 0, &hdrDecoded);
        	if(status != OS_STATUS_OK)
        	{
            	logError("sipHdrLenTime_getValue() fails to get Max-Forwards value.");
            	goto EXIT;
        	}

        	if(hdrDecoded.decodedIntValue == 1)
        	{
            	logError("Max-Forwards=1, drop the message.");
            	status = OS_ERROR_EXT_INVALID_VALUE;
            	goto EXIT;
        	}

        	hdrDecoded.decodedIntValue--;
			debug("to remove, decodedIntValue=%d", hdrDecoded.decodedIntValue);
            sipMsgAddHdr(pReqMsg->sipRequest, SIP_HDR_MAX_FORWARDS, &hdrDecoded.decodedIntValue, NULL, ctrl);
			break;
		}
		case SIP_HDR_VIA:
			debug("to remove, VIA, modeType=%d", pNMT->modType);
			if(pNMT->modType == SIP_HDR_MODIFY_TYPE_ADD)
			{
				sipHdrVia_t viaHdr={};
				viaHdr.sentProtocol[2].p = "UDP";
				viaHdr.sentProtocol[2].l = 3;
				sipConfig_getHost(&viaHdr.hostport.host, &viaHdr.hostport.portValue);
				sipHdrVia_generateBranchId(&pReqMsg->viaBranchId, "test");
				sipHdrParamNameValue_t branch={{"branch", 6}, pReqMsg->viaBranchId};
				viaHdr.pBranch = &branch;
				sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
				sipHdrParamNameValue_t param2={{"sigcomp-id", 10}, {"\"urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8\"", strlen("\"urn:uuid:cfb972de-d085-35a3-80f5-4206e4e124e8\"")}};
				osList_append(&viaHdr.viaParamList, &param1);
				osList_append(&viaHdr.viaParamList, &param2);
                sipMsgAddHdr(pReqMsg->sipRequest, SIP_HDR_VIA, &viaHdr, NULL, ctrl);
			}
			else if (pNMT->modType == SIP_HDR_MODIFY_TYPE_REPLACE)
			{
				sipHdrDecoded_u viaHdrDecoded;
				
				sipHdrGetValue(sipMsgInDecoded, SIP_HDR_VIA, 0, &viaHdrDecoded); 
				sipHdrMultiVia_t* pViaHdr= viaHdrDecoded.decodedValue;
				debug("to remove, pViaHdr=%p", pViaHdr);
				debug("to remove, via.host=%r, port=%r.", &pViaHdr->pVia->hdrValue.hostport.host, &pViaHdr->pVia->hdrValue.hostport.port);
				sipHdrParamNameValue_t param1={{"received", 8}, {"10.11.22.33", 11}};
				sipHdrParamNameValue_t param2={{"rport", 5}, {"5060", 4}};
                osList_append(&pViaHdr->pVia->hdrValue.viaParamList, &param1);
                osList_append(&pViaHdr->pVia->hdrValue.viaParamList, &param2);
                sipMsgAddHdr(pReqMsg->sipRequest, SIP_HDR_VIA, pViaHdr, NULL, ctrl);
			}
			break;
		case SIP_HDR_RECORD_ROUTE:
            if(pNMT->modType == SIP_HDR_MODIFY_TYPE_ADD)
            {
			    sipHdrRouteElementPT_t route;
			    sipUri_t uri;
			    sipParamUri_build(&uri, URI_TYPE_SIP, "seanR", 5, NULL, 0, "interlogic.com", 14, 5060);
			    osPointerLen_t rdn = {"route", 5};
			    sipHdrRoute_build(&route, &uri, &rdn);
			    sipMsgAddHdr(pReqMsg->sipRequest, SIP_HDR_ROUTE, &route, NULL, ctrl);
			}
			break;
	}

EXIT:
	DEBUG_END
	return status;
}
