#include "sipConfig.h"
#include "sipHeaderMisc.h"
#include "sipHdrFromto.h"
#include "sipHdrVia.h"
#include "sipHdrTypes.h"

#include "sipTU.h"



typedef struct sipTUHdrModifyInfo {
    bool isDelete;
    sipHdrName_e nameCode;
    size_t hdrStartPos;
    size_t hdrSkipLen;
    osPointerLen_t value;
} sipTUHdrModifyInfo_t;


static osStatus_e sipTU_updateMaxForards(sipRawHdr_t* pRawMF, uint32_t* pMFValue);
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
	osList_delete(pSortedModifyHdrList);

    if(status != OS_STATUS_OK)
    {
        pSipBufResp = osMem_deref(pSipBufResp);
    }

	return pSipBufResp;
}


//add a via, remove top Route if there is lr, decrease Max-Forwarded by 1
osMBuf_t* sipTU_buildProxyRequest(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipHdrRawValueId_t* extraDelHdrList, uint8_t delHdrNum, sipHdrRawValueStr_t* extraAddHdrList, uint8_t addHdrNum, sipTransViaInfo_t* pTransViaId)
{
    osStatus_e status = OS_STATUS_OK;
	osMBuf_t* pSipBufReq = NULL;
	osList_t* pSortedModifyHdrList = NULL;

    if(!pReqDecodedRaw || !pTransViaId)
    {
        logError("null pointer, pReqDecodedRaw=%p, pTransViaId=%p.", pReqDecodedRaw, pTransViaId);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipHdrRawValueId_t* pUpdatedExtraDelHdrList = extraDelHdrList;
	uint8_t updatedDelHdrNum = delHdrNum;
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
    	osPointerLen_t* pLR = sipParamNV_getValuefromList(&pRoute->pGNP->hdrValue.genericParam, &lrName);

    	if(pLR != NULL)
		{
			pUpdatedExtraDelHdrList = osMem_alloc(sizeof(sipHdrRawValueId_t)*(delHdrNum+1), NULL);
			pUpdatedExtraDelHdrList[0].nameCode = SIP_HDR_ROUTE;
			pUpdatedExtraDelHdrList[0].isTopOnly = true;

			for(int i=0; i<delHdrNum; i++)
			{
				pUpdatedExtraDelHdrList[i+1] = extraDelHdrList[i];
			}

			updatedDelHdrNum += 1;
		}
	}

	pSortedModifyHdrList = sipTU_sortModifyHdrList(pReqDecodedRaw, pUpdatedExtraDelHdrList, updatedDelHdrNum, extraAddHdrList, addHdrNum);
	if(updatedDelHdrNum != delHdrNum)
	{
		osMem_deref(pUpdatedExtraDelHdrList);
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

    //update MF in the original sipBuf
    status = sipTU_updateMaxForards(pReqDecodedRaw->msgHdrList[SIP_HDR_MAX_FORWARDS]->pRawHdr, NULL);

    //copy request line.
    status = osMBuf_writeBufRange(pSipBufReq, pReqDecodedRaw->sipMsgBuf.pSipMsg, 0, pReqDecodedRaw->sipMsgBuf.hdrStartPos, true);

    //insert a new via hdr as the first header
    status = sipTU_addOwnVia(pSipBufReq, NULL, NULL, &pTransViaId->branchId, &pTransViaId->host, &pTransViaId->port);

	status = sipTU_copyHdrs(pSipBufReq, pReqDecodedRaw, pReqDecodedRaw->sipMsgBuf.hdrStartPos, pSortedModifyHdrList);

EXIT:
    osList_delete(pSortedModifyHdrList);
    if(status != OS_STATUS_OK)
    {
        pSipBufReq = osMem_deref(pSipBufReq);
    }

    return pSipBufReq;
}



/* this must be the first function to call to build a sip response buffer
 * if isAddNullContent = TRUE, it is expected that Content-Length is at the end of the response, and this function is the only function to be called to build the response message
 * pReqDecodedRaw contains all parsed raw headers in a sip message 
*/
osMBuf_t* sipTU_buildUasResponse(sipMsgDecodedRawHdr_t* pReqDecodedRaw, sipResponse_e rspCode, sipHdrName_e sipHdrArray[], int arraySize, bool isAddNullContent)
{
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
            osPointerLen_t toTag;
            status = sipHdrFromto_generateTagId(&toTag, true);
            if(status != OS_STATUS_OK)
            {
                logError("fails to generateTagId.");
                goto EXIT;
            }

            status = sipMsgAddHdr(pSipBufResp, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], &toTag, ctrl);
            osPL_dealloc(&toTag);
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
        sipHdrAddCtrl_t ctrlCL = {false, true, false, NULL};
        status = sipMsgAddHdr(pSipBufResp, SIP_HDR_CONTENT_LENGTH, &contentLen, NULL, ctrlCL);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipMsgAddHdr for SIP_HDR_CONTENT_LENGTH.");
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
    }

EXIT:
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
            	osPointerLen_t toTag;
            	status = sipHdrFromto_generateTagId(&toTag, true);
            	if(status != OS_STATUS_OK)
            	{
                	logError("fails to generateTagId.");
                	goto EXIT;
            	}

            	status = sipMsgAddHdr(pSipBuf, sipHdrArray[i], pReqDecodedRaw->msgHdrList[sipHdrArray[i]], &toTag, ctrl);
            	osPL_dealloc(&toTag);
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

	sipHdrName_e* sipHdrArray = osMem_alloc(sizeof(sipHdrName_e)*(delHdrNum + addHdrNum), NULL);
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
		osMem_deref(sipHdrArray);
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
    osList_delete(pSortedModifyHdrList);
	osMem_deref(pMsgDecodedRaw);

    if(status != OS_STATUS_OK)
    {
        osMem_deref(pSipBufNew);
		pSipBufNew = pSipBuf;
    }
	else
	{
		osMem_deref(pSipBuf);
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


osStatus_e sipTU_asGetUser(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* sipUser, bool* isOrig)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pReqDecodedRaw || !sipUser || !isOrig)
    {
        logError("null pointer, pReqDecodedRaw=%p, sipUser=%p, isOrig=%p.", pReqDecodedRaw, sipUser, isOrig);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//by default, set isOrig = true
	*isOrig = true;
	
	//check if there is P-Server-User
	bool isOrigChecked = false;
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
		*sipUser = pPSU->uri.sipUser;
		osPointerLen_t sescaseName = {"sescase", 7};
		osPointerLen_t* pSescase = sipParamNV_getValuefromList(&pPSU->genericParam, &sescaseName);
		if(pSescase->p[0] == 'o' && osPL_strcmp(pSescase, "orig"))
		{
			*isOrig = true;
			isOrigChecked = true;
		}
		else if(pSescase->p[0] == 't' && osPL_strcmp(pSescase, "term"))
		{
			*isOrig = false;
			isOrigChecked = true;
		}

		osMem_deref(pPSU);
	}
	else if(pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY] != NULL)
	{
		status = sipParamUri_getUriFromRawHdrValue(&pReqDecodedRaw->msgHdrList[SIP_HDR_P_ASSERTED_IDENTITY]->pRawHdr->value, sipUser);
        if(status != OS_STATUS_OK)
        {
            logError("fails to sipParamUri_getUriFromRawHdrValue for SIP_HDR_P_ASSERTED_ID.");
            goto EXIT;
        }
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

	if(!isOrigChecked && pReqDecodedRaw->msgHdrList[SIP_HDR_ROUTE] != NULL)
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

		osMem_deref(pRoute);
	}

EXIT:
	return status;
}


/*
 * branchExtraStr: a string that caller wants to be inserted into branch ID.
 * pParamList: list of sipHdrParamNameValue_t, like: sipHdrParamNameValue_t param1={{"comp", 4}, {"sigcomp", 7}};
 * pParamList: a list of header parameters other than branchId.
 */
osStatus_e sipTU_addOwnVia(osMBuf_t* pMsgBuf, char* branchExtraStr, osList_t* pParamList, osPointerLen_t* pBranchId, osPointerLen_t* pHost, uint32_t* pPort)
{
	osStatus_e status = OS_STATUS_OK;

	if(pMsgBuf == NULL)
	{
		logError("null pointer, pMsgBuf.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    sipHdrVia_t viaHdr={};
    viaHdr.sentProtocol[2].p = "UDP";
    viaHdr.sentProtocol[2].l = 3;
    sipConfig_getHost(&viaHdr.hostport.host, &viaHdr.hostport.portValue);
    status = sipHdrVia_generateBranchId(pBranchId, branchExtraStr);
    if(status != OS_STATUS_OK)
    {
        logError("fails to generate via branch id.");
        goto EXIT;
    }

    sipHdrParamNameValue_t branch={{"branch", 6}, *pBranchId};
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
		*pHost = viaHdr.hostport.host;
		*pPort = viaHdr.hostport.portValue;
	}
		
	return status;
}


static osStatus_e sipTU_updateMaxForards(sipRawHdr_t* pRawMF, uint32_t* pMFValue)
{
	uint32_t mfValue = 0;
	osStatus_e status = OS_STATUS_OK;

	if(!pRawMF)
	{
		logError("null pointer, pRawMF.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	mfValue = osPL_str2u32(&pRawMF->value);
	if(mfValue == 0)
	{
		logError("max-forwards is 0.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osPL_modifyu32(&pRawMF->value, --mfValue);

EXIT:
	if(pMFValue)
	{
		*pMFValue = mfValue;
	}

	return status;
}



static osStatus_e sipTU_copyHdrs(osMBuf_t* pSipBuf, sipMsgDecodedRawHdr_t* pReqDecodedRaw, size_t startPos, osList_t* pSortedModifyHdrList)
{
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

			startPos = pHdrId->hdrStartPos + pHdrId->hdrSkipLen;

            //check if there is multiple values for the hdr
            if(sipHdr_isAllowMultiValue(pHdrId->nameCode))
            {
                sipHdrDecoded_t sipHdrDecoded = {};
                status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[pHdrId->nameCode]->pRawHdr, &sipHdrDecoded, false);
                if(status != OS_STATUS_OK)
                {
                    logError("fails to sipDecodeHdr for SIP_HDR_P_SERVED_USER.");
                    goto EXIT;
                }

                if(sipHdr_getHdrValueNum(&sipHdrDecoded) > 1)
                {
					sipHdr_posInfo_t firstHdrValuePos;
					sipHdr_getFirstHdrValuePosInfo(&sipHdrDecoded, &firstHdrValuePos);
					osMBuf_writeBufRange(pSipBuf, pReqDecodedRaw->sipMsgBuf.pSipMsg, pHdrId->hdrStartPos, firstHdrValuePos.startPos, true);
					startPos = firstHdrValuePos.startPos + firstHdrValuePos.totalLen;
#if 0
                   pHdrId->hdrSkipLen = sipHdr_getFirstHdrValueLen(&sipHdrDecoded);

	                osMBuf_writeStr(pSipBuf, sipHdr_getNameByCode(pHdrId->nameCode), true);
    	            osMBuf_writeStr(pSipBuf, ": ", true);
#endif
				}
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
            sipHdrGetRawHdrPos(pReqDecodedRaw->msgHdrList[pNewHdrInfo->nameCode]->pRawHdr, &pNewHdrInfo->hdrStartPos, &pNewHdrInfo->hdrSkipLen);
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
 * for del list, is isTopOnly, only delete the top hdr value, otherwise, delete all value for the hdr in the sip message
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

	pList = osMem_zalloc(sizeof(osList_t), NULL);
	if(!pList)
	{
		logError("fails to osMem_zalloc pList.");
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

        pHdrInfo = osMem_alloc(sizeof(sipTUHdrModifyInfo_t), NULL);

		pHdrInfo->nameCode = delHdrList[i].nameCode;
		pHdrInfo->isDelete = true;

        status = sipHdrGetRawHdrPos(pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->pRawHdr, &pHdrInfo->hdrStartPos, &pHdrInfo->hdrSkipLen);
        if(status != OS_STATUS_OK)
        {
            logInfo("fails to get hdrStartPos for nameCode (%d)", delHdrList[i]);
			osMem_deref(pHdrInfo);
            goto EXIT;
        }

		osList_orderAppend(pList, sipTU_hdrModiftListSortHandler, pHdrInfo, pReqDecodedRaw);

		//check if more than one hdr value to be removed
		if(!delHdrList[i].isTopOnly && pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->rawHdrNum > 1)
		{
			osList_t* pList = &pReqDecodedRaw->msgHdrList[delHdrList[i].nameCode]->rawHdrList;
			osListElement_t* pLE = pList->head;
			while (pLE)
			{
				pHdrInfo = osMem_alloc(sizeof(sipTUHdrModifyInfo_t), NULL);
		       	status = sipHdrGetRawHdrPos((sipRawHdr_t*)pLE->data, &pHdrInfo->hdrStartPos, &pHdrInfo->hdrSkipLen);
        		if(status != OS_STATUS_OK)
        		{
           			logInfo("fails to get hdrStartPos for nameCode (%d) in the rawHdrList", delHdrList[i].nameCode);
           			osMem_deref(pHdrInfo);
           			goto EXIT;
				} 

        		osList_orderAppend(pList, sipTU_hdrModiftListSortHandler, pHdrInfo, pReqDecodedRaw);
				
				pLE = pLE->next;
			}
		}
	}

	for(int i=0; i<addHdrNum; i++)
	{
		sipTUHdrModifyInfo_t* pHdrInfo = osMem_alloc(sizeof(sipTUHdrModifyInfo_t), NULL);

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
