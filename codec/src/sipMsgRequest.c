/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipMsgRequest.c
 ********************************************************/

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "osMemory.h"
#include "osMBuf.h"
#include "osList.h"

#include "sipHeader.h"
#include "sipMsgFirstLine.h"
#include "sipMsgRequest.h"
#include "sipConfig.h"
#include "sipHdrMisc.h"



static void sipMsgDecodedRawHdr_delete(void* data);
static bool sipHdr_listSortFunc(osListElement_t *le1, osListElement_t *le2, void *arg);
static void sipMsgResponse_delete(void* data);
static void sipMsgRequest_delete(void* data);
extern osStatus_e sipProxyHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgRequest_t* pReqMsg, sipHdrNmT_t* pNMT);
extern osStatus_e sipServerCommonHdrBuildEncode(sipMsgDecoded_t* sipMsgInDecoded, sipMsgResponse_t* pResp, sipHdrName_e hdrCode);


sipMsgRequest_t* sipMsgCreateReq(sipRequest_e reqType, sipUri_t* pReqUri)
{
    osStatus_e status = OS_STATUS_OK;
    sipMsgRequest_t* pSipRequest = NULL;

    if(!pReqUri)
    {
        logError("Null pointer, pReqUri.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipRequest = oszalloc(sizeof(sipMsgRequest_t), sipMsgRequest_delete);
    if(pSipRequest == NULL)
    {
        logError("pSipRequest allocation fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipRequest->sipRequest = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(pSipRequest->sipRequest == NULL)
    {
        logError("pSipRequest->sipRequest allocation fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }
    osMBuf_t* pSipBuf = pSipRequest->sipRequest;

    pSipRequest->reqType = reqType;

    //req line
    sipReqLinePT_t reqLine;
	reqLine.sipReqCode = reqType;
	reqLine.pSipUri = pReqUri;
    status = sipHdrFirstline_encode(pSipBuf, &reqLine, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("encode the request fails, reqType=%d.", reqType);
        goto EXIT;
    }

	//end of hdr.  if new hdr is added after that, the addhdr function will override these two characters.
    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pSipRequest);
		pSipRequest = NULL;
	}

	return pSipRequest;
}


//if ctrl.isRaw = true, and pExtraInfo != NULL, the specified hdrName shall only have one header value
osStatus_e sipMsgAddHdr(osMBuf_t* pSipBuf, sipHdrName_e hdrName, void* pHdr, void* pExtraInfo, sipHdrAddCtrl_t ctrl)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;

	if(!pSipBuf || !pHdr)
	{
		logError("null pointer, pSipBuf=%p, pHdr=%p.", pSipBuf, pHdr);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(ctrl.isOverride2Bytes)
    {
		//rewind 2 bytes to override \r\n
    	osMBuf_advance(pSipBuf, -2);
	}

	sipHdrEncode_h encodeHandler = sipHdrGetEncode(hdrName);
	if(!encodeHandler)
	{
		logError("sipHdrGetEncode returns null for hdr (%d).", hdrName);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(ctrl.isRaw)
	{
        sipRawHdrList_t* pHdrList = pHdr;
		if(pHdrList->rawHdrNum <= 1)
		{
            if(ctrl.newHdrName)
            {
                osMBuf_writeStr(pSipBuf, ctrl.newHdrName, true);
            }
            else
            {
                osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->name, true);
            }
            osMBuf_writeBuf(pSipBuf, ": ", 2, true);
            osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->value, true);
			//pExtraInfo only applies to the 1st header value.  If pExtraInfo is not NULL, it is expected the header only has one value.
			if(pExtraInfo)
			{
				osMBuf_writeU8(pSipBuf, ';', true);
				osMBuf_writePL(pSipBuf, (osPointerLen_t*)pExtraInfo, true);
			}
            osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
		}
		else
		{
			if(!ctrl.isRevertHdrValue)
			{
                if(ctrl.newHdrName)
                {
                    osMBuf_writeStr(pSipBuf, ctrl.newHdrName, true);
                }
                else
                {
                    osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->name, true);
                }
                osMBuf_writeBuf(pSipBuf, ": ", 2, true);
                osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->value, true);
                osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
			}

			osList_t* pList = &pHdrList->rawHdrList;
			osListElement_t* pLE = ctrl.isRevertHdrValue ? pList->tail : pList->head;
        	while(pLE)
        	{
           		sipRawHdr_t* pHdrRaw = pLE->data;
				if(ctrl.newHdrName)
				{
					osMBuf_writeStr(pSipBuf, ctrl.newHdrName, true);
				}
				else
				{ 
           			osMBuf_writePL(pSipBuf, &pHdrRaw->name, true);
				}
           		osMBuf_writeBuf(pSipBuf, ": ", 2, true);
           		osMBuf_writePL(pSipBuf, &pHdrRaw->value, true);
           		osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);

				pLE = ctrl.isRevertHdrValue ? pLE->prev : pLE->next;
        	}

            if(ctrl.isRevertHdrValue)
            {
                if(ctrl.newHdrName)
                {
                    osMBuf_writeStr(pSipBuf, ctrl.newHdrName, true);
                }
                else
                {
                    osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->name, true);
                }
                osMBuf_writeBuf(pSipBuf, ": ", 2, true);
                osMBuf_writePL(pSipBuf, &pHdrList->pRawHdr->value, true);
                osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
            }
		}
	}
	else
	{
    	status = encodeHandler(pSipBuf, pHdr, pExtraInfo ? pExtraInfo : &hdrName);
    	if(status != OS_STATUS_OK)
    	{
        	logError("sipHdrEncode_handler() fails for SIP_HDR_MODIFY_TYPE_ADD for hdr (%d).", hdrName);
        	goto EXIT;
    	}
	}

	if(ctrl.isOverride2Bytes)
	{
    	//add the end of sip hdr (a new line)
    	osMBuf_writeStr(pSipBuf, "\r\n", true);
	}	
EXIT:
	DEBUG_END
	return status;
}


//append a new hdr at the bottom of the current existing hdrs of a sip message under construction
//if hdrValue == NULL, implies the hdr value is digits, use hdrNumValue
osStatus_e sipMsgAppendHdrStr(osMBuf_t* pSipBuf, char* hdrName, osPointerLen_t* hdrValue, size_t hdrNumValue)
{
    DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !hdrName)
    {
        logError("null pointer, pSipBuf=%p, hdrName=%p.", pSipBuf, hdrName);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    osMBuf_writeStr(pSipBuf, hdrName, true);
    osMBuf_writeBuf(pSipBuf, ": ", 2, true);
	if(hdrValue != NULL)
	{
    	osMBuf_writePL(pSipBuf, hdrValue, true);
	}
	else
	{
		if(hdrNumValue <= 0xff)
		{
			osMBuf_writeU8Str(pSipBuf, hdrNumValue, true);
		}
		else if(hdrNumValue <= 0xffff)
		{
			osMBuf_writeU16Str(pSipBuf, hdrNumValue, true);
        }
		else if(hdrNumValue <= 0xffffffff)
        {
            osMBuf_writeU32Str(pSipBuf, hdrNumValue, true);
        }
		else
		{
            osMBuf_writeU64Str(pSipBuf, hdrNumValue, true);
        }
	}
    osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipMsgAppendContent(osMBuf_t* pSipBuf, osPointerLen_t* content, bool isProceedNewline)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf)
    {
        logError("null pointer, pSipBuf=%p.", pSipBuf);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(isProceedNewline)
	{
    	osMBuf_writeStr(pSipBuf, "\r\n", true);
	}

	osMBuf_writePL(pSipBuf, content, true);

EXIT:
	return status;
}


//insert a namevalue pair to the end of the current hdr that is ended by pSipBuf->pos.  The hdr shall already have \r\n added
osStatus_e sipMsgHdrAppend(osMBuf_t* pSipBuf, sipHdrParamNameValue_t* paramPair, char seperator)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pSipBuf || !paramPair)
	{
		logError("null pointer, pSipBuf=%p, paramPair=%p.", pSipBuf, paramPair);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    //rewind 2 bytes to override \r\n, end of hdr
    osMBuf_advance(pSipBuf, -2);
	
	osMBuf_writeU8(pSipBuf, seperator, true);
	osMBuf_writePL(pSipBuf, &paramPair->name, true);
	osMBuf_writeU8(pSipBuf, '=', true);
	osMBuf_writePL(pSipBuf, &paramPair->value, true);

	osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);

EXIT:
	return status;
}


//allocate sipMsgProxyRequest_t memory
sipMsgRequest_t* sipMsgCreateProxyReq(sipMsgDecoded_t* sipMsgInDecoded, sipHdrNmT_t nmt[], int n)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
    sipMsgRequest_t* pSipReq = NULL;
	osList_t modifyHdrList={};

    if(!sipMsgInDecoded || !nmt || n==0)
    {
        logError("null pointer, sipMsgInDecoded=%p, nmt=%p, n=%d.", sipMsgInDecoded, nmt, n);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    pSipReq = oszalloc(sizeof(sipMsgRequest_t), sipMsgRequest_delete);
    if(!pSipReq)
    {
        logError("fails to allocate memory for sipMsgRequest_t.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    sipHdrModifyInfo_t* modifyInfo = oszalloc(n*sizeof(sipHdrModifyInfo_t), NULL);
	if(!modifyInfo)
	{
		logError("fail to allocate modifyInfo.");
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		goto EXIT;
	}

    for(int i=0; i<n; i++)
    {
        modifyInfo[i].nmt = nmt[i];
        osList_append(&modifyHdrList, &modifyInfo[i]);
    }

    pSipReq->sipRequest = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(!pSipReq->sipRequest)
    {
        logError("fails to allocate memory for pSipReq->sipRequest.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

	osListElement_t* pLE = modifyHdrList.head;
	while(pLE)
    {
        sipHdrModifyInfo_t* pHdrModifyInfo = pLE->data;
    	status = sipHdrCreateProxyModifyInfo(pHdrModifyInfo, sipMsgInDecoded);
    	if(status != OS_STATUS_OK)
    	{
        	logError("sipHdrCreateModifyInfo() fails for hdr (%d).", pHdrModifyInfo->nmt.hdrCode);
        	goto EXIT;
    	}

		pLE = pLE->next;
	}
    //sort the list
	osList_sort(&modifyHdrList, sipHdr_listSortFunc, NULL);

	//process hdr one after another, including hdr building and encoding
    osMBuf_t* sipMsgOut = pSipReq->sipRequest;
    osMBuf_t* sipMsgIn = sipMsgInDecoded->sipMsg;
    size_t sipMsgInStartPos = 0;
    pLE = modifyHdrList.head;
    while(pLE)
    {
        sipHdrModifyInfo_t* pHdrModifyInfo = pLE->data;

        size_t len = pHdrModifyInfo->origHdrPos - sipMsgInStartPos;
		debug("to remove 4, len=%ld, hdrname=%s, modType=%d, origHdr=%ld, startPos=%ld", len, sipHdr_getNameByCode(pHdrModifyInfo->nmt.hdrCode), pHdrModifyInfo->nmt.modType, pHdrModifyInfo->origHdrPos, sipMsgInStartPos);
        if(len > 0)
        {
            osPointerLen_t sipMsgInStr = {&sipMsgIn->buf[sipMsgInStartPos], len};
            osMBuf_writePL(sipMsgOut, &sipMsgInStr, true);
        }
        sipMsgInStartPos = pHdrModifyInfo->origHdrPos;

        switch(pHdrModifyInfo->nmt.modType)
        {
            case SIP_HDR_MODIFY_TYPE_REMOVE:
				//"+2" to remove /r/n.  origHdrLen does not contain \r\n
                sipMsgInStartPos += pHdrModifyInfo->origHdrLen==0 ? 0 : pHdrModifyInfo->origHdrLen+2;
                break;
            case SIP_HDR_MODIFY_TYPE_ADD:
                status = sipProxyHdrBuildEncode(sipMsgInDecoded, pSipReq, &pHdrModifyInfo->nmt);
                if(status != OS_STATUS_OK)
                {
                    logError("sipMsgProxyBuildEncodeHdr() fails for SIP_HDR_MODIFY_TYPE_ADD of hdr (%d).", pSipReq->reqType);
                    goto EXIT;
                }
                break;
            case SIP_HDR_MODIFY_TYPE_REPLACE:
				//"+2" to remove /r/n.  origHdrLen does not contain \r\n
                sipMsgInStartPos += pHdrModifyInfo->origHdrLen==0 ? 0 : pHdrModifyInfo->origHdrLen+2;
                status = sipProxyHdrBuildEncode(sipMsgInDecoded, pSipReq, &pHdrModifyInfo->nmt);
                if(status != OS_STATUS_OK)
                {
                    logError("sipMsgProxyBuildEncodeHdr() fails for SIP_HDR_MODIFY_TYPE_REPLACE of hdr (%d).", pSipReq->reqType);
                    goto EXIT;
                }
                break;
            default:
                logError("incorrect pHdrModifyInfo->modeType (%d).", pHdrModifyInfo->nmt.modType);
                break;
        }

        pLE = pLE->next;
    }
	
    //copy the remaining sip message
    if(sipMsgInStartPos < sipMsgIn->end)
    {
        osPointerLen_t sipMsgInStr = {&sipMsgIn->buf[sipMsgInStartPos], sipMsgIn->end-sipMsgInStartPos};
        osMBuf_writePL(sipMsgOut, &sipMsgInStr, true);
    }

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pSipReq);
		pSipReq = NULL;
	}

	osfree(modifyInfo);
	osList_clear(&modifyHdrList);

	DEBUG_END
	return pSipReq;
}


/* include response line, via, from, callId*/
sipMsgResponse_t* sipMsgCreateResponse(sipMsgDecoded_t* sipMsgInDecoded, sipResponse_e rspCode, sipHdrName_e extraHdrList[], int n)
{
    osStatus_e status = OS_STATUS_OK;
    sipMsgResponse_t* pSipResponse = NULL;

    if(!sipMsgInDecoded)
    {
        logError("Null pointer, sipMsgInDecoded.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipResponse = oszalloc(sizeof(sipMsgResponse_t), sipMsgResponse_delete);
    if(pSipResponse == NULL)
    {
        logError("pSipRequest allocation fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipResponse->rspCode = rspCode;
    pSipResponse->pRequest = osmemref(sipMsgInDecoded);

    pSipResponse->sipMsg = osMBuf_alloc(SIP_MAX_MSG_SIZE);
    if(pSipResponse->sipMsg == NULL)
    {
        logError("fails to allocate pSipResponse->sipMsg.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }
    osMBuf_t* pSipBuf = pSipResponse->sipMsg;

    //status line
    status = sipHdrFirstline_respEncode(pSipBuf, &pSipResponse->rspCode, NULL);
    if(status != OS_STATUS_OK)
    {
        logError("fails to encode the response, respCode=%d.", rspCode);
        goto EXIT;
    }

    sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
	sipHdrName_e addHdrList[] = {SIP_HDR_VIA, SIP_HDR_FROM, SIP_HDR_CALL_ID, SIP_HDR_CSEQ};
	sipRawHdrList_t* hdrRawList = NULL;
	for (int i=0; i < sizeof(addHdrList)/sizeof(sipHdrName_e); i++) 	
	{
		hdrRawList = sipHdrGetRawValue(sipMsgInDecoded, addHdrList[i]);
		if(!hdrRawList)
		{
			logError("fails to get raw hdrRawList for hdrCode (%d).", addHdrList[i]);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		status = sipMsgAddHdr(pSipResponse->sipMsg, addHdrList[i], hdrRawList, NULL, ctrl);
		if(status != OS_STATUS_OK)
		{
        	logError("fails to get raw hdrRawList for SIP_HDR_FROM.");
        	status = OS_ERROR_INVALID_VALUE;
        	goto EXIT;
		}
    }

	for(int i=0; i<n; i++)
	{
		status = sipServerCommonHdrBuildEncode(sipMsgInDecoded, pSipResponse, extraHdrList[i]);
		if(status != OS_STATUS_OK)
		{
			logError("fails to sipServerHdrBuildEncode for hdrCode (%d).", extraHdrList[i]);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}
	}

    //end of hdr.  if new hdr is added after that, the addhdr function will override these two characters.
    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
    if(status != OS_STATUS_OK)
    {
        osfree(pSipResponse);
        pSipResponse = NULL;
    }

    return pSipResponse;
}


/* parse all hdr fields, do not decode any hdr. 
 * if sipHdrArray == NULL or hdrArraySize == 0, parse all
 */
sipMsgDecodedRawHdr_t* sipDecodeMsgRawHdr(sipMsgBuf_t* pSipMsgBuf, sipHdrName_e sipHdrArray[], int hdrArraySize)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
    sipMsgDecodedRawHdr_t* pSipMsgDecoded = NULL;

    if(!pSipMsgBuf)
    {
        logError("pSipHdrBuf is NULL.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osMBuf_t* pSipMsg = pSipMsgBuf->pSipMsg;
    if(!pSipMsg)
    {
        logError("pSipMsg is NULL.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(!sipHdrArray && hdrArraySize > 0)
    {
        logError("sipHdrArray is NULL but hdrArraySize > 0.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    size_t origPos = pSipMsg->pos;
	debug("sean-remove, before, hdrStartPos=%ld", pSipMsgBuf->hdrStartPos);
    //if pSipHdrBuf->hdrStartPos = 0, assume the hdrStartPos was not set
    if(pSipMsgBuf->hdrStartPos == 0)
    {
        pSipMsgBuf->hdrStartPos = sipHdr_getHdrStartPos(pSipMsg);
        if(pSipMsg->pos == 0)
        {
            logError("fails to sipMsg_getHdrStartPos.");
            status = OS_ERROR_NULL_POINTER;
            goto EXIT;
        }
	}

    pSipMsg->pos = pSipMsgBuf->hdrStartPos;
    debug("sean-remove, after, hdrStartPos=%ld, pos=%ld", pSipMsgBuf->hdrStartPos, pSipMsg->pos);

    pSipMsgDecoded = oszalloc(sizeof(sipMsgDecodedRawHdr_t), sipMsgDecodedRawHdr_delete);
    if(!pSipMsgDecoded)
    {
        logError("fails to oszalloc for pSipMsgDecoded.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        return NULL;
    }

    pSipMsgDecoded->sipMsgBuf = *pSipMsgBuf;

    bool isEOH= false;
    while(!isEOH)
    {
        sipRawHdr_t* pSipHdr = osmalloc(sizeof(sipRawHdr_t), NULL);

        //decode rawHdr for each hdr in the SIP message
        if(sipDecodeOneHdrRaw(pSipMsg, pSipHdr, &isEOH) != OS_STATUS_OK)
        {
            logError("sipDecodeOneHdrRaw error.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }

        //check the selective adding
        bool isAdd = false;
        if(!sipHdrArray || hdrArraySize == 0)
        {
            //always add
            isAdd = true;
        }
        if(sipHdrArray && hdrArraySize)
        {
            //selective add
            for(int i=0; i<hdrArraySize; i++)
            {
                if(sipHdrArray[i] == pSipHdr->nameCode)
                {
                    isAdd = true;
                    break;
                }
            }
        }
        if(!isAdd)
        {
            osfree(pSipHdr);
            continue;
        }

        if(pSipMsgDecoded->msgHdrList[pSipHdr->nameCode] == NULL)
        {
            pSipMsgDecoded->msgHdrList[pSipHdr->nameCode] = oszalloc(sizeof(sipRawHdrList_t), NULL);
            pSipMsgDecoded->hdrNum++;
        }

        if(pSipMsgDecoded->msgHdrList[pSipHdr->nameCode]->pRawHdr == NULL)
        {
            pSipMsgDecoded->msgHdrList[pSipHdr->nameCode]->pRawHdr = pSipHdr;
            pSipMsgDecoded->msgHdrList[pSipHdr->nameCode]->rawHdrNum = 1;
        }
        else
        {
            osList_append(&pSipMsgDecoded->msgHdrList[pSipHdr->nameCode]->rawHdrList, pSipHdr);
            ++pSipMsgDecoded->msgHdrList[pSipHdr->nameCode]->rawHdrNum;
        }
    }

	osMBuf_allocRef2(&pSipMsgDecoded->msgContent, pSipMsg, pSipMsg->pos, pSipMsg->end - pSipMsg->pos);

EXIT:
    if(status != OS_STATUS_OK)
    {
        pSipMsgDecoded = osfree(pSipMsgDecoded);
		pSipMsgDecoded = NULL;
    }

    if(status != OS_ERROR_NULL_POINTER)
    {
        pSipMsg->pos = origPos;
    }

	DEBUG_END
    return pSipMsgDecoded;
}


static void sipMsgDecodedRawHdr_delete(void* data)
{
    if(!data)
    {
        return;
    }

    sipMsgDecodedRawHdr_t* pMsgDecoded = data;

    for (int i=0; i<SIP_HDR_COUNT; i++)
    {
        if(pMsgDecoded->msgHdrList[i] == NULL)
        {
            continue;
        }

        osfree(pMsgDecoded->msgHdrList[i]->pRawHdr);
        osList_delete(&pMsgDecoded->msgHdrList[i]->rawHdrList);
		osfree(pMsgDecoded->msgHdrList[i]);
    }
}


/* check if a hdr has multiple values, it can be due to multiple headers with the same header name, or one header name with multiple values
 * isCheckTopHdrOnly: true, only check if there are multiple values in the top hdr entry of the specified hdr name
 *                    false, check if there are multiple hdr values in the whole message for the specified hdr name
 * pTopHdrDecoded: Not NULL, the caller wants the top hdr entry be decoded and passed out, the caller needs to free sipHdrDecoded when done
 *                 NULL, the caller does not want the decoded top hdr entry 
 */
bool sipMsg_isHdrMultiValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, bool isCheckTopHdrOnly, sipHdrDecoded_t* pTopHdrDecoded)
{
	osStatus_e status = OS_STATUS_OK;
    bool isMulti = false;
	
	if(pTopHdrDecoded)
	{
        pTopHdrDecoded->decodedHdr = NULL;
        pTopHdrDecoded->isRawHdrCopied = false;
	}

    if(!pReqDecodedRaw)
    {
        logError("null pointer, pReqDecodedRaw.");
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pReqDecodedRaw->msgHdrList[hdrCode] == NULL)
	{
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//the hdr only allows one hdr value, isMulti = false
    if(!sipHdr_isAllowMultiValue(hdrCode))
    {
		goto EXIT;
	}

	//if check the specified hdr in the whole message, check rawHdrNum first
	if(!isCheckTopHdrOnly && pReqDecodedRaw->msgHdrList[hdrCode]->rawHdrNum > 1)
	{
		isMulti = true;
		goto EXIT;
	}

	//isCheckTopHdrOnly==true, or isCheckTopHdrOnly==false but there is only one hdr entry for  specified hdr name in the whole message
    bool isMultiHdrValuePossible = false;
    for(int i=0; i<pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr->value.l; i++)
    {
        if(pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr->value.p[i] == ',')
        {
            isMultiHdrValuePossible = true;
            break;
        }

        if(isMultiHdrValuePossible)
        {
            sipHdrDecoded_t sipHdrDecoded = {};
            if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr, &sipHdrDecoded, false) != OS_STATUS_OK)
            {
                logError("fails to sipDecodeHdr for hdr code(%d).", hdrCode);
                goto EXIT;
            }

            if(sipHdr_getHdrValueNum(&sipHdrDecoded) > 1)
            {
                isMulti = true;
            }

			if(pTopHdrDecoded)
			{
				*pTopHdrDecoded = sipHdrDecoded;
			}
			else
			{
            	osfree(sipHdrDecoded.decodedHdr);
			}

			goto EXIT;
        }
    }

EXIT:
	if(status == OS_STATUS_OK && pTopHdrDecoded && pTopHdrDecoded->decodedHdr == NULL)
	{
		if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[hdrCode]->pRawHdr, pTopHdrDecoded, false) != OS_STATUS_OK)
        {
            logError("fails to sipDecodeHdr for hdr code(%d).", hdrCode);
			pTopHdrDecoded->decodedHdr == NULL;
        }
	}

    return isMulti;
}


osStatus_e sipHdrMultiNameParam_get2ndHdrValue(sipHdrName_e hdrCode, sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrDecoded_t* pFocusHdrDecoded, sipHdrGenericNameParamDecoded_t** pp2ndHdr)
{
    osStatus_e status = OS_STATUS_OK;

	if(!pReqDecodedRaw || !pFocusHdrDecoded || !pp2ndHdr)
	{
		logError("null pointer, pReqDecodedRaw=%p, pFocusHdrDecoded=%p, pp2ndHdr=%p.", pReqDecodedRaw, pFocusHdrDecoded, pp2ndHdr);
		return OS_ERROR_NULL_POINTER;
	}

	pFocusHdrDecoded->decodedHdr = NULL;

    bool isMulti = sipMsg_isHdrMultiValue(hdrCode, pReqDecodedRaw, false, pFocusHdrDecoded);
    if(isMulti)
    {
        if(sipHdr_getHdrValueNum(pFocusHdrDecoded) > 1)
        {
            //get the nexthop from the top hdr
            *pp2ndHdr = ((sipHdrMultiGenericNameParam_t*)pFocusHdrDecoded->decodedHdr)->gnpList.head->data;
        }
        else
        {
            //get the nexthop from the 2nd hdr
            if(sipDecodeHdr(pReqDecodedRaw->msgHdrList[hdrCode]->rawHdrList.head->data, pFocusHdrDecoded, false) != OS_STATUS_OK)
            {
                logError("fails to sipDecodeHdr for the 2nd route header.");
                status = OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }

            *pp2ndHdr = ((sipHdrMultiGenericNameParam_t*)pFocusHdrDecoded->decodedHdr)->pGNP;
        }

        if(!*pp2ndHdr)
        {
            logError("expect 2nd route, but decoded=null.");
            osfree(pFocusHdrDecoded->decodedHdr);
            pFocusHdrDecoded->decodedHdr = NULL;

            status = OS_ERROR_SYSTEM_FAILURE;
            goto EXIT;
        }
    }

EXIT:
    return status;
}

 
bool sipHdr_listSortFunc(osListElement_t *le1, osListElement_t *le2, void *arg)
{
	//assume le1 and le2 are not null, it is the caller's responsibility to check the null pointer.
	sipHdrModifyInfo_t* pModifyInfo1 = le1->data;
	sipHdrModifyInfo_t* pModifyInfo2 = le2->data;
	bool isReverse = !arg ? false : *(bool*)arg;
	bool status = true;

	int delta = pModifyInfo1->origHdrPos - pModifyInfo2->origHdrPos;
	if(delta < 0)
	{
		status = isReverse ? false : true;
		goto EXIT;
	}
	else if(delta == 0)
	{
		//if the same hdr requires a add and a replace, like Via to replace old, add new, the order shall be "add then replace"
		if(pModifyInfo1->nmt.modType == SIP_HDR_MODIFY_TYPE_ADD)
		{
			status = isReverse ? false : true;
			goto EXIT;
		}
	}

	status = isReverse ? true : false;

EXIT:
	return status;
}



void sipMsgRequest_delete(void* data)
{
	if (!data)
	{
		return;
	}

	sipMsgRequest_t* pReq = data;
	osMBuf_dealloc(pReq->sipRequest);
	osfree((void*)pReq->viaBranchId.p);
	osfree((void*)pReq->fromTag.p);
//	osfree((void*)pReq->toTag.p);
	osfree((void*)pReq->callId.p);
}

void sipMsgResponse_delete(void* data)
{
    if (!data)
    {
        return;
    }

	sipMsgResponse_t* pResp = data;
	osMBuf_dealloc(pResp->sipMsg);
	osfree((void*)pResp->pRequest);
    osfree((void*)pResp->toTag.p);
}
