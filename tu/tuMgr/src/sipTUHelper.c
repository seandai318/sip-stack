/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUHelper.c
 ********************************************************/

#include "osDebug.h"
#include "osPL.h"

#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipHdrFromto.h"
#include "sipHdrVia.h"
#include "sipHdrTypes.h"
#include "sipHdrMisc.h"

#include "sipTU.h"



typedef struct sipTUHdrModifyInfo {
    bool isDelete;
	bool isDelTopOnly;		//=1, only delete the top hdr value, even if the hdr has multiple hdr value, otherwise, delete all hdrs that has the specified hdr name
    sipHdrName_e nameCode;
    size_t hdrStartPos;
    size_t hdrSkipLen;
    osPointerLen_t value;
} sipTUHdrModifyInfo_t;


static osStatus_e sipTU_updateMaxForwards(sipRawHdr_t* pRawMF, bool isForce, uint32_t* pMFValue);
static bool sipTU_hdrModiftListSortHandler(osListElement_t* le, osListElement_t* newLE, void* arg);
static osList_t* sipTU_sortModifyHdrList(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* addHdrList, uint8_t addHdrNum);
static osStatus_e sipTU_copyHdrs(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, size_t startPos, osList_t* pSortedModifyHdrList);



/*
 * remove top via, and hdr in delHdrList, and add hdr in addHdrList
 * the same hdr shall not show 2 or more times in delHdrList, for each hdr in the delHdrList, it can set isTopOnly.
 * the same hdr can show multiple times in addHdrList, for the same hdr, the order of hdr value in addHdrList will maintain in the sip message  
 * the same hdr can show both in delHdrList and addHdrList.
 * the delHdrList must at least contain a Via hdr.
 *
 * the delete hdr only deletes the top hdr value.  if a hdr has multiple values, and you want to delete all the hdr appearance in a sip message, you have to use a second pass to specially delete the hdr.
 */
osMBuf_t* sipTU_buildProxyResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* addHdrList, uint8_t addHdrNum)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    osMBuf_t* pSipBufResp = NULL;
	osList_t* pSortedModifyHdrList = NULL;

    if(!pReqDecodedRaw || !delHdrList)
    {
        logError("null pointer, pReqDecodedRaw=%p, delHdrList=%p.", pReqDecodedRaw, delHdrList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	bool hasVIA = false;
	for(int i=0; i<delHdrNum; i++)
	{
		if(delHdrList[i].nameCode == SIP_HDR_VIA)
		{
			hasVIA = true;
			break;
		}
	}

	if(!hasVIA)
	{
		logError("the delHdrList does not contain VIA header.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    pSipBufResp = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufResp)
    {
        logError("fails to allocate pSipBufResp.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    pSortedModifyHdrList = sipTU_sortModifyHdrList(pReqDecodedRaw, delHdrList, delHdrNum, addHdrList, addHdrNum);
	if(!pSortedModifyHdrList)
	{
		logError("fails to sipTU_sortModifyHdrList.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    //copy response line.
    status = osMBuf_writeBufRange(pSipBufResp, pReqDecodedRaw->sipMsgBuf.pSipMsg, 0, pReqDecodedRaw->sipMsgBuf.hdrStartPos, true);

    status = sipTU_copyHdrs(pSipBufResp, pReqDecodedRaw, pReqDecodedRaw->sipMsgBuf.hdrStartPos, pSortedModifyHdrList);

EXIT:
	osfree(pSortedModifyHdrList);
	//osList_delete(pSortedModifyHdrList);

    if(status != OS_STATUS_OK)
    {
        pSipBufResp = osfree(pSipBufResp);
    }

	DEBUG_END
	return pSipBufResp;
}


//add a via, remove top Route if there is lr, decrease Max-Forwarded by 1
//pTransViaId is IN/OUT, as IN, it passes the real peer's IP/port, when OUT, it contains the top via's branch ID, and host/port
//if isProxy=false, a new call id is to be generated, there is no need for external to delete/modify/specify a new callid
osMBuf_t* sipTU_b2bBuildRequest(sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isProxy, sipHdrRawValueId_t* extraDelHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* extraAddHdrList, uint8_t addHdrNum, sipTransViaInfo_t* pTransViaId, sipUri_t* pReqlineUri, size_t* pViaProtocolPos)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
	osMBuf_t* pSipBufReq = NULL;
	osList_t* pSortedModifyHdrList = NULL;
    sipHdrDecoded_t viaHdr={};

    if(!pReqDecodedRaw || !pTransViaId)
    {
        logError("null pointer, pReqDecodedRaw=%p, pTransViaId=%p.", pReqDecodedRaw, pTransViaId);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//update the hdr delete list
	sipHdrRawValueId_t* pUpdatedExtraDelHdrList = extraDelHdrList;
	uint8_t updatedDelHdrNum = delHdrNum;
#if 0	//ROUTE deletion logic is handled by caller, if needed, pass in from extraDelHdrList, this function does not handle it
	osPointerLen_t* pLR = NULL;
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE])
	{
	    //remove the top Route if there is lr, and copy the remaining sipBuf
	    sipHdrDecoded_t sipHdrDecoded = {};
    	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE]->pRawHdr, &sipHdrDecoded, false);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
        	goto EXIT;
    	}

    	sipHdrRoute_t* pRoute = sipHdrDecoded.decodedHdr;
    	osPointerLen_t lrName = {"lr", 2};
    	pLR = sipParamNV_getValuefromList(&pRoute->pGNP->hdrValue.genericParam, &lrName);
	}
#endif

	int extraDelItem = 1;		//1 is count for via
#if 0	//do not handle specifically for Route header in this function
	if(pLR)
	{
		++extraDelItem;
	}
#endif
	if(!isProxy)
	{
		++extraDelItem;		//for call id
	}
	updatedDelHdrNum += extraDelItem;

	pUpdatedExtraDelHdrList = osmalloc(sizeof(sipHdrRawValueId_t)*(delHdrNum + extraDelItem), NULL);

	--extraDelItem;
    //via will be reinserted again if isProxy=true, the reason for that is to handle the rport, otherwise just drop the via
    pUpdatedExtraDelHdrList[extraDelItem].nameCode = SIP_HDR_VIA;
    pUpdatedExtraDelHdrList[extraDelItem].isTopOnly = true;

#if 0	//do not handle specifically for Route header in this function	
	if(pLR)
	{
    	--extraDelItem;
    	pUpdatedExtraDelHdrList[extraDelItem].nameCode = SIP_HDR_ROUTE;
    	pUpdatedExtraDelHdrList[extraDelItem].isTopOnly = true;
	}
#endif
	if(!isProxy)
	{
		 --extraDelItem;
        pUpdatedExtraDelHdrList[extraDelItem].nameCode = SIP_HDR_CALL_ID;
        pUpdatedExtraDelHdrList[extraDelItem].isTopOnly = false;
    }

	extraDelItem = updatedDelHdrNum - delHdrNum;

	if(extraDelHdrList != NULL)
	{
    	for(int i=0; i<delHdrNum; i++)
    	{
        	pUpdatedExtraDelHdrList[i+extraDelItem] = extraDelHdrList[i];
    	}
	}

	pSortedModifyHdrList = sipTU_sortModifyHdrList(pReqDecodedRaw, pUpdatedExtraDelHdrList, updatedDelHdrNum, extraAddHdrList, addHdrNum);
	if(updatedDelHdrNum != delHdrNum)
	{
		osfree(pUpdatedExtraDelHdrList);
	}

    if(!pSortedModifyHdrList)
    {
        logError("fails to sipTU_sortModifyHdrList.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	//allocate the new sipRequest buffer
    pSipBufReq = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufReq)
    {
        logError("fails to allocate pSipBufReq.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

logError("to-remove, VIA-MEMORY, 1");
	//prepare for the building of new request
    status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &viaHdr, false);
    if(status != OS_STATUS_OK)
    {
        logError("fails to decode the top via hdr in sipDecodeHdr.");
        goto EXIT;
    }
logError("to-remove, VIA-MEMORY, 2");

    //update MF in the original sipBuf
	if(isProxy)
	{
    	status = sipTU_updateMaxForwards(pReqDecodedRaw->msgHdrList[SIP_HDR_MAX_FORWARDS]->pRawHdr, false, NULL);
	}
	else
	{
		uint32_t mfValue = 70;
		status = sipTU_updateMaxForwards(pReqDecodedRaw->msgHdrList[SIP_HDR_MAX_FORWARDS]->pRawHdr, true, &mfValue);
	}

    //on request line.
	if(pReqlineUri)
    {
		osPointerLen_t method;
		status = sipMsg_code2Method(pReqDecodedRaw->sipMsgBuf.reqCode, &method);

		osPointerLen_t src = {pReqDecodedRaw->sipMsgBuf.pSipMsg->buf, pReqDecodedRaw->sipMsgBuf.hdrStartPos};
		osMBuf_writeUntil(pSipBufReq, &src, &method, true);
		osMBuf_writeU8(pSipBufReq, ' ', true);
		osMBuf_writePL(pSipBufReq, &pReqlineUri->sipUser, true);
		if(pReqlineUri->hostport.portValue != 0)
		{
			status = osMBuf_writeU8(pSipBufReq, ':', true);
			status = osMBuf_writeU32Str(pSipBufReq, pReqlineUri->hostport.portValue, true);
		}
		status = osMBuf_writeStr(pSipBufReq, " SIP/2.0\r\n", true);
	}
	else
	{
		status = osMBuf_writeBufRange(pSipBufReq, pReqDecodedRaw->sipMsgBuf.pSipMsg, 0, pReqDecodedRaw->sipMsgBuf.hdrStartPos, true);
	}

    //insert a new via hdr as the first header
	sipHostport_t viaHostport;
    status = sipTU_addOwnVia(pSipBufReq, NULL, NULL, &pTransViaId->branchId, &viaHostport.host, &viaHostport.portValue, pViaProtocolPos);
logError("to-remove, VIA-MEMORY, 2-1");

	//add back the received via if isProxy=true
	if(isProxy)
	{
	    sipHostport_t peer;
    	peer.host = pTransViaId->host;
    	peer.portValue = pTransViaId->port;

		logError("to-remove, PEER, host=%r, port=%d", &peer.host, peer.portValue);
    	status = sipHdrVia_rspEncode(pSipBufReq, viaHdr.decodedHdr, pReqDecodedRaw, &peer);
	}
	else
	{
		//create and insert call-id if not proxy
		status = sipHdrCallId_createAndAdd(pSipBufReq, NULL);
	}

logError("to-remove, VIA-MEMORY, 2-2");

	status = sipTU_copyHdrs(pSipBufReq, pReqDecodedRaw, pReqDecodedRaw->sipMsgBuf.hdrStartPos, pSortedModifyHdrList);

	//update pTransViaId host/port to reflect the transaction top via's host/port
	pTransViaId->host = viaHostport.host;
	pTransViaId->port = viaHostport.portValue;
logError("to-remove, VIA-MEMORY, 2-3");

EXIT:
	osfree(pSortedModifyHdrList);
    //osList_delete(pSortedModifyHdrList);
    if(status != OS_STATUS_OK)
    {
        pSipBufReq = osfree(pSipBufReq);
    }

logError("to-remove, VIA-MEMORY, 3");
	osfree(viaHdr.decodedHdr);
logError("to-remove, VIA-MEMORY, 4");

	DEBUG_END
    return pSipBufReq;
}


//create a UAC request with req line, via, from, to, callId and max forward.  Other headers needs to be added by user as needed
//be noted this function does not include the extra "\r\n" at the last of header, user needs to add it when completing the creation of a SIP message
osMBuf_t* sipTU_uacBuildRequest(sipRequest_e code, sipUri_t* pReqlineUri, osPointerLen_t* called, osPointerLen_t* caller, sipTransViaInfo_t* pTransViaId, size_t* pViaProtocolPos)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    osMBuf_t* pSipBufReq = NULL;

	if(!pReqlineUri || !called || !caller)
	{
		logError("null pointer, pReqlineUri=%p, called=%p, caller=%p.", pReqlineUri, called, caller);
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipBufReq = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufReq)
    {
        logError("fails to allocate pSipBufReq.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    osPointerLen_t method;
    status = sipMsg_code2Method(code, &method);
	osMBuf_writePL(pSipBufReq, &method, true);

    osMBuf_writeU8(pSipBufReq, ' ', true);
    osMBuf_writePL(pSipBufReq, &pReqlineUri->sipUser, true);
    if(pReqlineUri->hostport.portValue != 0)
    {
        status = osMBuf_writeU8(pSipBufReq, ':', true);
        status = osMBuf_writeU32Str(pSipBufReq, pReqlineUri->hostport.portValue, true);
    }
    status = osMBuf_writeStr(pSipBufReq, " SIP/2.0\r\n", true);

    //insert a new via hdr as the first header
    sipHostport_t viaHostport;
    status = sipTU_addOwnVia(pSipBufReq, NULL, NULL, &pTransViaId->branchId, &viaHostport.host, &viaHostport.portValue, pViaProtocolPos);

	//encode to header
	osMBuf_writeStr(pSipBufReq, "To: <", true);
	osMBuf_writePL(pSipBufReq, called, true);
	osMBuf_writeStr(pSipBufReq, ">\r\n", true);

	//encode from header
    osMBuf_writeStr(pSipBufReq, "From: <", true);
    osMBuf_writePL(pSipBufReq, caller, true);
    osMBuf_writeStr(pSipBufReq, ">;", true);

    osDPointerLen_t fromTag;
    status = sipHdrFromto_generateTagId((osPointerLen_t*)&fromTag, true);
    if(status != OS_STATUS_OK)
    {
    	logError("fails to generateTagId.");
        goto EXIT;
    }
	osMBuf_writePL(pSipBufReq, (osPointerLen_t*)&fromTag, true);
	osMBuf_writeStr(pSipBufReq, "\r\n", true);
	osDPL_dealloc(&fromTag);

	status = sipHdrCallId_createAndAdd(pSipBufReq, NULL);

	//encode max-forward
	osMBuf_writeStr(pSipBufReq, "Max-Forwards: 69\r\n", true);

EXIT:
	DEBUG_END
	return pSipBufReq;
}


/* this must be the first function to call to build a sip response buffer
 * if isAddNullContent = TRUE, it is expected that Content-Length is at the end of the response, and this function is the only function to be called to build the response message
 * pReqDecodedRaw contains all parsed raw headers in a sip message 
*/
osMBuf_t* sipTU_buildUasResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipResponse_e rspCode, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pReqDecodedRaw || !sipHdrArray)
    {
        logError("null pointer, pReqDecodedRaw=%p, sipHdrArray=%p.", pReqDecodedRaw, sipHdrArray);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osMBuf_t* pSipBufResp = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufResp)
    {
        logError("fails to allocate pSipBufResp.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

//    sipHdrName_e sipHdrArray[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_TO, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
//    int arraySize = sizeof(sipHdrArray) / sizeof(sipHdrArray[0]);

    //encode status line
    status = sipHdrFirstline_respEncode(pSipBufResp, &rspCode, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to encode the response, respCode=%d.", rspCode);
        goto EXIT;
    }

    sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
    for(int i=0; i<arraySize; i++)
    {
        if(sipHdrArray[i] == SIP_HDR_TO)
        {
            osDPointerLen_t toTag;
            status = sipHdrFromto_generateTagId((osPointerLen_t*)&toTag, true);
            if(status != OS_STATUS_OK)
            {
                logError("fails to generateTagId.");
                goto EXIT;
            }

            status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], (osPointerLen_t*)&toTag, ctrl);
            osDPL_dealloc(&toTag);
        }
        else
        {
            status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], NULL, ctrl);
        }

        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
            goto EXIT;
        }
    }

    if(isAddNullContent)
    {
        uint32_t contentLen = 0;
        sipHdrAddCtrl_t ctrlCL = {false, false, false, NULL};
        status = sipMsgAddHdr(pSipBufResp, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
	
		//add a empty line at the end of the message
		osMBuf_writeBuf(pSipBufResp, "\r\n", 2, true);	
    }

EXIT:
	DEBUG_END

    return pSipBufResp;
}


/* when this function is called, sipTU_buildUasResponse must have been called
 * if sipHdrArray = NULL, or arraySize = 0, no hdr will be added, except for the possible NULL Content
 * if isAddNullContent = TRUE, it is expected this is the last function to build the sip response
 * the hdrs in the sipHdrArray are copied from the src to the dest sip message in the order of they are inputed in the sipHdrArray, and are copied location starts from the current pos in pSipBuf
 */
//static osStatus_e sipTU_buildSipMsgCont(osMBuf_t* pSipBufResp, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
osStatus_e sipTU_copySipMsgHdr(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !pReqDecodedRaw)
    {
        logError("null pointer, pSipBuf= %p, pReqDecodedRaw=%p.", pSipBuf, pReqDecodedRaw);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	logError("sean-remove, sipHdrArray=%p, arraySize=%d", sipHdrArray, arraySize);
	if(sipHdrArray)
	{
    	sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
//    	osPointerLen_t extraInfo;
    	for(int i=0; i<arraySize; i++)
    	{
			logError("sean-remove, i=%d, sipHdrArray[i]=%d", i, sipHdrArray[i]);
        	if(sipHdrArray[i] == SIP_HDR_TO)
        	{
            	osDPointerLen_t toTag;
            	status = sipHdrFromto_generateTagId((osPointerLen_t*)&toTag, true);
            	if(status != OS_STATUS_OK)
            	{
                	logError("fails to generateTagId.");
                	goto EXIT;
            	}

            	status = sipMsgAddHdr(pSipBuf, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], (osPointerLen_t*)&toTag, ctrl);
            	osDPL_dealloc(&toTag);
        	}
        	else
        	{
				logError("sean-remove, i=%d, sipHdrArray[i]=%d", i, sipHdrArray[i]);
            	status = sipMsgAddHdr(pSipBuf, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], NULL, ctrl);
        	}

        	if(status != OS_STATUS_OK)
        	{
            	logError("fails to sipMsgAddHdr for respCode (%d).", sipHdrArray[i]);
            	goto EXIT;
        	}
    	}
	}

    if(isAddNullContent)
    {
        uint32_t contentLen = 0;
        sipHdrAddCtrl_t ctrlCL = {false, false, false, NULL};
        status = sipMsgAddHdr(pSipBuf, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
    }

EXIT:
	DEBUG_END
    return status;
}


/*
 * return the modified pSipMsgBuf (if the function is executed without error) or the original pSipMsgBuf (if there is error in the execution of the function)
 */
osMBuf_t* sipTU_modifySipMsgHdr(osMBuf_t* pSipBuf, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* addHdrList, uint8_t addHdrNum)
{
    osStatus_e status = OS_STATUS_OK;
    osMBuf_t* pSipBufNew = NULL;
	sipMsgDecodedRawHdr_t* pMsgDecodedRaw = NULL;
    osList_t* pSortedModifyHdrList = NULL;

    if(!pSipBuf || !delHdrList)
    {
        logError("null pointer, pSipBuf=%p, delHdrList=%p.", pSipBuf, delHdrList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipHdrName_e* sipHdrArray = osmalloc(sizeof(sipHdrName_e)*(delHdrNum + addHdrNum), NULL);
	for(int i=0; i<delHdrNum; i++)
	{
		sipHdrArray[i] = delHdrList->nameCode;
	}
	for(int j=0; j<addHdrNum; j++)
	{
		sipHdrArray[j] = addHdrList->nameCode;
	}

	//reqCode and isRequest in sipMsgBuf is irrelevant here.
	sipMsgBuf_t sipMsgBuf = {pSipBuf, 0, SIP_METHOD_INVALID, true};
	pMsgDecodedRaw = sipDecodeMsgRawHdr(&sipMsgBuf, sipHdrArray, delHdrNum + addHdrNum);
	if(!pMsgDecodedRaw)
	{
		logError("fails to sipDecodeMsgRawHdr.");
		osfree(sipHdrArray);
        goto EXIT;
    }

    pSortedModifyHdrList = sipTU_sortModifyHdrList(pMsgDecodedRaw, delHdrList, delHdrNum, addHdrList, addHdrNum);
    if(!pSortedModifyHdrList)
    {
        logError("fails to sipTU_sortModifyHdrList.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    pSipBufNew = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipBufNew)
    {
        logError("fails to allocate pSipBufNew.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    //copy the first line.
    status = osMBuf_writeBufRange(pSipBufNew, pMsgDecodedRaw->sipMsgBuf.pSipMsg, 0, pMsgDecodedRaw->sipMsgBuf.hdrStartPos, true);

    status = sipTU_copyHdrs(pSipBufNew, pMsgDecodedRaw, pMsgDecodedRaw->sipMsgBuf.hdrStartPos, pSortedModifyHdrList);

EXIT:
	osfree(pSortedModifyHdrList);
    //osList_delete(pSortedModifyHdrList);
	osfree(pMsgDecodedRaw);

    if(status != OS_STATUS_OK)
    {
        osfree(pSipBufNew);
		pSipBufNew = pSipBuf;
    }
	else
	{
		osfree(pSipBuf);
	}

    return pSipBufNew;
}


/* pDecodedHdrValue is the decoded hdr value data structure used as an  input for the encoding, it may be unit32_t/char string/a hdr data structure, depending the hdr.
 * extraInfo is the extrainfo for encoding that may be used in some hdr.  consult a particular hdr's encoding for the exact meaning of extraInfo
 */
osStatus_e sipTU_addMsgHdr(osMBuf_t* pSipBuf, sipHdrName_e hdrCode, void* pDecodedHdrValue, void* extraInfo)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipBuf)
	{
		logError("null pointer, pSipBuf.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipHdrAddCtrl_t ctrl = {false, false, false, NULL};
	status = sipMsgAddHdr(pSipBuf, hdrCode, pDecodedHdrValue, extraInfo, ctrl);

EXIT:
	return status;
}


//add contact and Expires.  For Expires, use standalone Expires first if it exists in the SIP REGISTER, otherwise, use expires in contact hdr.
osStatus_e sipTU_addContactHdr(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t regExpire)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

	if(!pSipBuf || !pReqDecodedRaw)
	{
		logError("null pointer, pSipBuf=%p, pReqDecodedRaw=%p.", pSipBuf, pReqDecodedRaw);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	//if pReqDecodedRaw contains Expires header, just use it for Expires
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES])
	{
		status = sipTU_addMsgHdr(pSipBuf, SIP_HDR_EXPIRES, &regExpire, NULL);
		if(status != OS_STATUS_OK)
		{
			logError("fails to add hdr Expires using sipTU_addMsgHdr.");
			goto EXIT;
		}
	}

	//use received contact hdr
	sipHdrName_e sipHdrName = SIP_HDR_CONTACT;
	status = sipTU_copySipMsgHdr(pSipBuf, pReqDecodedRaw, &sipHdrName, 1, false);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipTU_copySipMsgHdr for header contact.");
		goto EXIT;
	}
	
EXIT:
	DEBUG_END
	return status;
}


//based on parameter's in the p-served-user or top route, if not find, assume it is orig
osStatus_e sipTU_asGetSescase(sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool* isOrig)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pReqDecodedRaw || !isOrig)
    {
        logError("null pointer, pReqDecodedRaw=%p, isOrig=%p.", pReqDecodedRaw, isOrig);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//by default, assume the AS serves as orig
	*isOrig = true;
    if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER] != NULL)
    {
        sipHdrDecoded_t sipHdrDecoded = {};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER]->pRawHdr, &sipHdrDecoded, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
            goto EXIT;
        }

        sipHdrType_pServedUser_t* pPSU = sipHdrDecoded.decodedHdr;
        osPointerLen_t sescaseName = {"sescase", 7};
        osPointerLen_t* pSescase = sipParamNV_getValuefromList(&pPSU->genericParam, &sescaseName);
        if(pSescase->p[0] == 'o' && osPL_strcmp(pSescase, "orig"))
        {
            *isOrig = true;
        }
        else if(pSescase->p[0] == 't' && osPL_strcmp(pSescase, "term"))
        {
            *isOrig = false;
        }

        osfree(pPSU);

		goto EXIT;
    }

    if(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE] != NULL)
    {
        sipHdrDecoded_t sipHdrDecoded = {};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE]->pRawHdr, &sipHdrDecoded, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
            goto EXIT;
        }

        sipHdrRoute_t* pRoute = sipHdrDecoded.decodedHdr;
        osPointerLen_t sescaseName = {"orig", 4};
        *isOrig = sipParamNV_isNameExist(&pRoute->pGNP->hdrValue.genericParam, &sescaseName);

        osfree(pRoute);
		goto EXIT;
    }

EXIT:
	return status;
}


//the order of check: p-served-user, then p-asserted-user, then for mo, from, for mt, request-uri, to
osStatus_e sipTU_asGetUser(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* sipUser, bool isOrigUser, bool isOrigAS)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pReqDecodedRaw || !sipUser)
    {
        logError("null pointer, pReqDecodedRaw=%p, sipUser=%p.", pReqDecodedRaw, sipUser);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//check if there is P-Server-User
	if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER] != NULL)
	{
		sipHdrDecoded_t sipHdrDecoded = {};
		status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_P_SERVED_USER]->pRawHdr, &sipHdrDecoded, false);
		if(status != OS_STATUS_OK)
		{
			logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
			goto EXIT;
		}

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

		osfree(pPSU);

		if(isOrigUser == isSesCaseMO)
		{
			*sipUser = pPSU->uri.sipUser;
			goto EXIT;
		}
	}

	if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY] != NULL)
	{
		//for original AS, the PAI is caller, for terminating AS, the PAI is called
		if(isOrigUser == isOrigAS)
		{
			status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY]->pRawHdr->value, sipUser);
        	if(status != OS_STATUS_OK)
        	{
            	logError("fails to sipParamUri_getUriFromRawHdrValue for SIP_HDR_P_ASSERTED_ID.");
        	}
			goto EXIT;
		}
	}

	if(isOrigUser)
	{
		status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_FROM]->pRawHdr->value, sipUser);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipParamUri_getUriFromRawHdrValue for SIP_HDR_FROM.");
		}
            
		goto EXIT;
	}
	else
	{
		status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_TO]->pRawHdr->value, sipUser);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipParamUri_getUriFromRawHdrValue for SIP_HDR_TO.");
            goto EXIT;
        }
    }

EXIT:
	return status;
}


/*
 * pBranchId can be NULL.  If it is NULL, the generated branchId is to be cleared at the end of this function.
 * branchExtraStr: a string that caller wants to be inserted into branch ID.
 * pParamList: list of sipHdrParamNameValue_t, like: sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
 * pParamList: a list of header parameters other than branchId.
 */
osStatus_e sipTU_addOwnVia(osMBuf_t* pMsgBuf, char* branchExtraStr, osList_t* pParamList, osPointerLen_t* pBranchId, osPointerLen_t* pHost, uint32_t* pPort, size_t* pProtocolViaPos)
{
	osStatus_e status = OS_STATUS_OK;
	osPointerLen_t branchId={};

	if(pMsgBuf == NULL)
	{
		logError("null pointer, pMsgBuf.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	*pProtocolViaPos = pMsgBuf->pos + 13;  		//len 0f 13 = "Via: SIP/2.0/"

    sipHdrVia_t viaHdr={};
    viaHdr.sentProtocol[2].p = "UDP";
    viaHdr.sentProtocol[2].l = 3;
    sipConfig_getHost(&viaHdr.hostport.host, &viaHdr.hostport.portValue);
    status = sipHdrVia_generateBranchId(&branchId, branchExtraStr);
    if(status != OS_STATUS_OK)
    {
        logError("fails to generate via branch id.");
        goto EXIT;
    }

    sipHdrParamNameValue_t branch={{"branch", 6}, branchId};
	viaHdr.pBranch = &branch;
	if(pParamList != NULL)
	{
		osListElement_t* pLE = pParamList->head;
		while(pLE)
		{
    		osList_append(&viaHdr.viaParamList, pLE->data);
			pLE = pLE->next;
		}
	}

	status =  sipTU_addMsgHdr(pMsgBuf, SIP_HDR_VIA, &viaHdr, NULL);
    
	osList_clear(&viaHdr.viaParamList);

EXIT:
	if(status == OS_STATUS_OK)
	{
		if(pHost)
		{
			*pHost = viaHdr.hostport.host;
		}
		if(pPort)
		{
			*pPort = viaHdr.hostport.portValue;
		}
	}
	
	if(pBranchId)
	{
		*pBranchId = branchId;
	}
	else
	{
		osDPL_dealloc((osDPointerLen_t*) &branchId);
	}
	
	return status;
}


//other req may also be a in-session req, like REFER, etc. but they shall be taken care of in UA.  These req will be pass through in proxy
bool sipTU_isProxyCallReq(sipRequest_e reqCode)
{
	switch (reqCode)
	{
		case SIP_METHOD_INVITE:
		case SIP_METHOD_ACK:
		case SIP_METHOD_BYE:
		case SIP_METHOD_UPDATE:
		case SIP_METHOD_CANCEL:
			return true;
			break;
		default:
			return false;
			break;
	}
}



osStatus_e sipTU_msgBuildEnd(osMBuf_t* pSipBuf, bool isExistContent)
{
	if(!pSipBuf)
	{
		logError("null pointer, pSipBuf.");
		return OS_ERROR_NULL_POINTER;
	}

	if(!isExistContent)
	{
        osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
	}

	return OS_STATUS_OK;
}


//if isForce=true, pMFValue is IN, pass the value to the function, otherwise, a OUT
static osStatus_e sipTU_updateMaxForwards(sipRawHdr_t* pRawMF, bool isForce, uint32_t* pMFValue)
{
	uint32_t mfValue = 0;
	osStatus_e status = OS_STATUS_OK;

	if(!pRawMF)
	{
		logError("null pointer, pRawMF.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(isForce && !pMFValue)
	{
		logError("null pMFValue while isForce=true.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(isForce)
	{
		mfValue = *pMFValue;
	}
	else
	{
		mfValue = osPL_str2u32(&pRawMF->value);
		if(mfValue == 0)
		{
			logError("max-forwards is 0.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}
		--mfValue;
	}

	osPL_modifyu32(&pRawMF->value, --mfValue);

EXIT:
	if(pMFValue)
	{
		*pMFValue = mfValue;
	}

	return status;
}


//for the deletion in the modifyHdrList, only delete the 1st hdr value if there is more than one value for a hdr or multiple hdrs for the same hdr name
static osStatus_e sipTU_copyHdrs(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, size_t startPos, osList_t* pSortedModifyHdrList)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    if(!pSipBuf || !pReqDecodedRaw)
    {
        logError("null pointer, pSipBuf=%p, pReqDecodedRaw=%p,", pSipBuf, pReqDecodedRaw);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(!pSortedModifyHdrList)
    {
        status = osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, startPos, pReqDecodedRaw->sipMsgBuf.pSipMsg->end, true);
        goto EXIT;
    }

    osListElement_t* pLE = pSortedModifyHdrList->head;
    while(pLE)
    {
        sipTUHdrModifyInfo_t* pHdrId = pLE->data;
        if(pHdrId->isDelete)
        {
            status = osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, startPos, pHdrId->hdrStartPos, true);

			//skip the whole hdr entry
			startPos = pHdrId->hdrStartPos + pHdrId->hdrSkipLen;

            //check if there are multiple values for the hdr, if yes, update the startPos to only skip the 1st value of the hdr entry
			if(pHdrId->isDelTopOnly)
			{
				sipHdrDecoded_t sipHdrDecoded = {};
				if(sipMsg_isHdrMultiValue(pHdrId->nameCode, pReqDecodedRaw, true, &sipHdrDecoded))
                {
					sipHdr_posInfo_t firstHdrValuePos;
					sipHdr_getFirstHdrValuePosInfo(&sipHdrDecoded, &firstHdrValuePos);
					size_t nextHdrValueStartPos = pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr->valuePos;	
					//this is needed to include the hdr name, be noted the nextHdrValueStartPos is used instead of firstHdrValuePos.startPos, the reason is that when pReqDecodedRaw is used to decode a hdr, the top hdr value's startPos is always 0
					osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, pHdrId->hdrStartPos, nextHdrValueStartPos, true);

					startPos = nextHdrValueStartPos + firstHdrValuePos.totalLen;
				}
						        
				osfree(sipHdrDecoded.decodedHdr);	
            }
        }
        else
        {
            if(startPos < pHdrId->hdrStartPos)
            {
                if(pHdrId->hdrStartPos == SIP_HDR_EOF)
                {
                    status = osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, startPos, pReqDecodedRaw->sipMsgBuf.pSipMsg->end, true);
                    startPos = pHdrId->hdrStartPos;
                }
            }
            else if(startPos > pHdrId->hdrStartPos)
            {
                logError("unexpected event, startPos (%d) > pHdrId->hdrStartPos(%d) when inserting a new hdr.", startPos, pHdrId->hdrStartPos);
            }

            osMBuf_writePL(pSipBuf, &pHdrId->value, true);
        }

        pLE = pLE->next;
    }

    if(startPos != SIP_HDR_EOF && startPos < pReqDecodedRaw->sipMsgBuf.pSipMsg->end)
    {
        status = osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, startPos, pReqDecodedRaw->sipMsgBuf.pSipMsg->end, true);
    }


EXIT:
	DEBUG_END
    return status;
}


/* the assumption is the list already been filled with delete hdr before processing the add hdr
 * 1. if the same hdr for add and delete, delete has priority
 * 2. if the same hdr for add, the later added hdr value will be added before the existing hdr value in the sorted list (that effectively implies that in the final constructed sip message, the later added hdr value will appear after the earlier added hdr) 
 * 2. for delete hdr, sort based on its existing position.
 * 3. if a hdr to be added already has a existing hdr, add as the top hdr for the existing hdr
 * 4. if a hdr to be added does not have existing hdr, add before content len hdr. 
 * 5. if content len does not exist, add to the end of sip hdr
 */
static bool sipTU_hdrModiftListSortHandler(osListElement_t* le, osListElement_t* newLE, void* arg)
{
	bool isPrepend = false;

	sipMsgDecodedRawHdr_t* pReqDecodedRaw = arg;
	sipTUHdrModifyInfo_t* pHdrInfo = le->data;
	sipTUHdrModifyInfo_t* pNewHdrInfo = newLE->data;

    //for delete hdr
    if(pNewHdrInfo->isDelete)
    {
		if(pNewHdrInfo->hdrStartPos < pHdrInfo->hdrStartPos)
		{
			isPrepend = true;
		}
		goto EXIT;
	}
	else
	{
		//since we always insert delList first, and the same hdr in delList is not allowed, the addList for the same nameCode will always after the hdr in the delList, and the same hdr in the add List will be always FILO 
		if(pNewHdrInfo->nameCode == pHdrInfo->nameCode)
		{
			if(!pHdrInfo->isDelete)
			{
				isPrepend = true;
			}
			goto EXIT;
		}	
			
		if(pReqDecodedRaw->msgHdrList[pNewHdrInfo->nameCode])
		{
        	sipHdrGetRawHdrPos(pReqDecodedRaw->msgHdrList[pNewHdrInfo->nameCode]->pRawHdr, &pNewHdrInfo->hdrStartPos, &pNewHdrInfo->hdrSkipLen);
			pNewHdrInfo->hdrSkipLen = 0;
		}
		else if(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTENT_LENGTH])
		{
            sipHdrGetRawHdrPos(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTENT_LENGTH]->pRawHdr, &pNewHdrInfo->hdrStartPos, &pNewHdrInfo->hdrSkipLen);
		}
		else
		{
			pNewHdrInfo->hdrStartPos = SIP_HDR_EOF;
		}

        pNewHdrInfo->hdrSkipLen = 0;
	}     
	
EXIT:
	return isPrepend;
}


/* generate a list of sipTUHdrModifyInfo_t element for each header
 * if the same hdr is to be added and removed, this is basically a replace action, will do remove first, then add
 * algorithm (based on the assumption the add and rmv lists are both small)
 * 1. combine them into one hdr list
 * 2. if the same hdr for add and delete, delete has priority
 * 3. for delete hdr, sort based on its existing position.
 * 4. if a hdr to be added already has a existing hdr, add as the top hdr for the existing hdr
 * 5. if a hdr to be added does not have existing hdr, add before content len hdr.
 * 6. if content len does not exist, add to the end of sip hdr
 *
 * for del list, if isTopOnly, only delete the top hdr value, otherwise, delete all value for the hdr in the sip message
 * for add list, can have multiple same hdr, the sorted list will be FILO, which corresponds FIFO in the final sip message
 * the del list can have the same hdr that appears in add list, vice versa
 * the added hdr ALWAYS the top hdr values.
 */
static osList_t* sipTU_sortModifyHdrList(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrRawValueId_t* delHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* addHdrList, uint8_t addHdrNum)
{
	osStatus_e status = OS_STATUS_OK;

	osList_t* pList = NULL;
	if(!pReqDecodedRaw)
	{
		logError("null pointer, pReqDecodedRaw=%p.", pReqDecodedRaw);
		goto EXIT;
	}

	pList = oszalloc(sizeof(osList_t), osList_cleanup);
	if(!pList)
	{
		logError("fails to oszalloc pList.");
		goto EXIT;
	}

	size_t hdrStartPos, hdrSkipLen;
	sipTUHdrModifyInfo_t* pHdrInfo = NULL;
	for(int i=0; i<delHdrNum; i++)
	{
		if(!pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode])
		{
			debug("try to delete a hdr (%d), but it does not exist in the original SIP message.", delHdrList[i].nameCode);
			continue;
		}

        pHdrInfo = osmalloc(sizeof(sipTUHdrModifyInfo_t), NULL);

		pHdrInfo->nameCode = delHdrList[i].nameCode;
		pHdrInfo->isDelete = true;
		pHdrInfo->isDelTopOnly = delHdrList[i].isTopOnly;

        status = sipHdrGetRawHdrPos(pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->pRawHdr, &pHdrInfo->hdrStartPos, &pHdrInfo->hdrSkipLen);
        if(status != OS_STATUS_OK)
        {
            logInfo("fails to get hdrStartPos for nameCode (%d)", delHdrList[i]);
			osfree(pHdrInfo);
            goto EXIT;
        }

		osList_orderAppend(pList, sipTU_hdrModiftListSortHandler, pHdrInfo, NULL);

		//check if more than one hdr line to be removed (a hdr line means a whole hdr entry in a SIP message, hdr-name: hdrvalue1, hdrvalue2...
		if(!delHdrList[i].isTopOnly && pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->rawHdrNum > 1)
		{
			osList_t* pList = &pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->rawHdrList;
			osListElement_t* pLE = pList->head;
			while (pLE)
			{
				pHdrInfo = osmalloc(sizeof(sipTUHdrModifyInfo_t), NULL);
		       	status = sipHdrGetRawHdrPos((sipRawHdr_t*)pLE->data, &pHdrInfo->hdrStartPos, &pHdrInfo->hdrSkipLen);
        		if(status != OS_STATUS_OK)
        		{
           			logInfo("fails to get hdrStartPos for nameCode (%d) in the rawHdrList", delHdrList[i].nameCode);
           			osfree(pHdrInfo);
           			goto EXIT;
				} 

        		osList_orderAppend(pList, sipTU_hdrModiftListSortHandler, pHdrInfo, NULL);
				
				pLE = pLE->next;
			}
		}
	}

	for(int i=0; i<addHdrNum; i++)
	{
		sipTUHdrModifyInfo_t* pHdrInfo = osmalloc(sizeof(sipTUHdrModifyInfo_t), NULL);

        pHdrInfo->nameCode = addHdrList[i].nameCode;
		pHdrInfo->value = addHdrList[i].value;
        pHdrInfo->isDelete = false;

		osList_orderAppend(pList, sipTU_hdrModiftListSortHandler, pHdrInfo, pReqDecodedRaw);
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		osList_delete(pList);
		pList = NULL;
	}

	return pList;
}
