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

    pSipRequest = osMem_zalloc(sizeof(sipMsgRequest_t), sipMsgRequest_delete);
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
		osMem_deref(pSipRequest);
		pSipRequest = NULL;
	}

	return pSipRequest;
}

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

    pSipReq = osMem_zalloc(sizeof(sipMsgRequest_t), sipMsgRequest_delete);
    if(!pSipReq)
    {
        logError("fails to allocate memory for sipMsgRequest_t.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    sipHdrModifyInfo_t* modifyInfo = osMem_zalloc(n*sizeof(sipHdrModifyInfo_t), NULL);
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
		osMem_deref(pSipReq);
		pSipReq = NULL;
	}

	osMem_deref(modifyInfo);
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

    pSipResponse = osMem_zalloc(sizeof(sipMsgResponse_t), sipMsgResponse_delete);
    if(pSipResponse == NULL)
    {
        logError("pSipRequest allocation fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pSipResponse->rspCode = rspCode;
    pSipResponse->pRequest = osMem_ref(sipMsgInDecoded);

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
        osMem_deref(pSipResponse);
        pSipResponse = NULL;
    }

    return pSipResponse;
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
	osMem_deref((void*)pReq->viaBranchId.p);
	osMem_deref((void*)pReq->fromTag.p);
//	osMem_deref((void*)pReq->toTag.p);
	osMem_deref((void*)pReq->callId.p);
}

void sipMsgResponse_delete(void* data)
{
    if (!data)
    {
        return;
    }

	sipMsgResponse_t* pResp = data;
	osMBuf_dealloc(pResp->sipMsg);
	osMem_deref((void*)pResp->pRequest);
    osMem_deref((void*)pResp->toTag.p);
}
