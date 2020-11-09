/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrNameValue.c
 ********************************************************/

#include "osDebug.h"
#include "osList.h"
#include "osMemory.h"

#include "sipParsing.h"
#include "sipHdrNameValue.h"
#include "sipHeader.h"


/* covers header Allow, Supported, Allow-Events */
osStatus_e sipParserHdr_nameList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isCaps, sipHdrNameList_t* pStrList)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pStrList)
    {
        logError("NULL pointer, pSipMsg=%p, pStrList=%p.", pSipMsg, pStrList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pStrList->nNum = 0;

    bool isName = true;
    size_t nameLen = 0;
    size_t origPos = pSipMsg->pos;
    while (pSipMsg->pos < hdrEndPos)
    {
        if((isCaps && SIP_IS_ALPHA_CAPS(pSipMsg->buf[pSipMsg->pos])) || (!isCaps && SIP_IS_ALPHA(pSipMsg->buf[pSipMsg->pos])))
        {
            pSipMsg->pos++;
            nameLen++;
            if(!isName)
            {
                origPos = pSipMsg->pos;
                isName = true;
            }
        }
        else if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
        {
            pSipMsg->pos++;
        }
        else if(pSipMsg->buf[pSipMsg->pos] == ',')
        {
            isName = false;
            pSipMsg->pos++;

            status = osList_addString(&pStrList->nameList, &pSipMsg->buf[origPos], nameLen);
            if(status != OS_STATUS_OK)
            {
                logError("add name to the nameList failure.");
                goto EXIT;
            }
            pStrList->nNum++;
            nameLen = 0;
        }
        else
        {
            logError("invalid character (%c)", pSipMsg->buf[pSipMsg->pos]);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
        }
    }

    //last parameter name
    status = osList_addString(&pStrList->nameList, &pSipMsg->buf[origPos], nameLen);
    if(status != OS_STATUS_OK)
    {
        logError("add name to the nameList failure.");
        goto EXIT;
    }
    pStrList->nNum++;

EXIT:
    if(status != OS_STATUS_OK)
    {
        osList_clear(&pStrList->nameList);
    }

    pSipMsg->pos++;
    return status;
}


osStatus_e sipParserHdr_nameValueList(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrNameValueList_t* pNVList)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pNVList)
    {
        logError("NULL pointer, pSipMsg=%p, pNVList=%p.", pSipMsg, pNVList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	pNVList->pNVP = NULL;

    sipParsingInfo_t parentParsingInfo;
    parentParsingInfo.arg = &pNVList->nvpList;
    sipParsingStatus_t parsingStatus;
	if(is4Multi)
	{
    	parentParsingInfo.tokenNum = 1;
    	parentParsingInfo.token[0]=',';
	}
	else
	{
		parentParsingInfo.tokenNum = 0;
	}	

    status = sipParserHdr_genericParam(pSipMsg, hdrEndPos, &parentParsingInfo, &parsingStatus);
    if(status != OS_STATUS_OK)
    {
    	logError("accepted contact parsing error.")
        goto EXIT;
    }

	pNVList->pNVP = sipParamNV_takeTopNVfromList(&pNVList->nvpList, &pNVList->nvpNum);

EXIT:
	if(status != OS_STATUS_OK)
	{
		osList_clear(&pNVList->nvpList);
	}

    return status;
}


osStatus_e sipParserHdr_multiNameValueList(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiNameValueList_t* pMultiNVList)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pMultiNVList)
    {
        logError("NULL pointer, pSipMsg=%p, pMultiNVList=%p.", pSipMsg, pMultiNVList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	pMultiNVList->nvNum = 0;
	pMultiNVList->pNV = NULL;
	osList_init(&pMultiNVList->nvList);

    while(pSipMsg->pos < hdrEndPos)
    {
		sipHdrNameValueListDecoded_t* pNV = oszalloc(sizeof(sipHdrNameValueListDecoded_t), sipHdrNameValueListDecoded_cleanup);
		if(!pNV)
		{
			logError("fails to allocate sipHdrNameValueListDecoded_t* pNV.");
			status = OS_ERROR_MEMORY_ALLOC_FAILURE;
			goto EXIT;
		}

		pNV->hdrPos.startPos = pSipMsg->pos;
		status = sipParserHdr_nameValueList(pSipMsg, hdrEndPos, true, &pNV->hdrValue);
        if(status != OS_STATUS_OK)
        {
            logError("sipParserHdr_nameValueList parsing error.");
            osfree(pNV);
            goto EXIT;
        }
		pNV->hdrPos.totalLen = pSipMsg->pos - pNV->hdrPos.startPos;

		if(pMultiNVList->nvNum++ == 0)
		{
			pMultiNVList->pNV = pNV;
		}
		else
		{
            osListElement_t* pLE = osList_append(&pMultiNVList->nvList, pNV);
            if(pLE == NULL)
            {
                logError("osList_append failure.");
                osfree(pNV);
                status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                goto EXIT;
            }
        }
    }

EXIT:
	if(status != OS_STATUS_OK && status!= OS_ERROR_NULL_POINTER)
    {
		osfree(pMultiNVList->pNV);
		osList_delete(&pMultiNVList->nvList);
	}
		
	return status;
}


osStatus_e sipParserHdr_MethodParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMethodParam_t* pMP)
{
	static sipParsingABNF_t sipMethodParamABNF[]={  \
    	{0, SIPP_MAX_PARAM_NUM, SIP_TOKEN_INVALID, 1, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL}, \
    	{0, 1,                  ',',               0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL}, \
    	{0, 1,                  SIP_TOKEN_EOH,     0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL}};
	static int sipP_methodParamNum = sizeof(sipMethodParamABNF) / sizeof(sipParsingABNF_t);

	osStatus_e status = OS_STATUS_OK;

	if(!pSipMsg || !pMP)
	{
		logError("null pointer, pSipMsg=%p, pMP=%p.", pSipMsg, pMP);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pMP->method.p = &pSipMsg->buf[pSipMsg->pos];
	pMP->method.l = 0;
	size_t origPos = pSipMsg->pos;
	while(pSipMsg->pos < hdrEndPos)
	{
		if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
		{
			pMP->method.l = pSipMsg->pos++ - origPos;
			break;
		}

		++pSipMsg->pos;
	} 	

	//bypass the preceding LWS
	while(pSipMsg->pos < hdrEndPos)
	{
		if(!SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
		{
			break;
		}

		++pSipMsg->pos;
    }

	if(pSipMsg->pos == hdrEndPos)
	{
		logError("the hdr does not contain mandatory parameter.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    //start parsing header parameters
	sipParsingInfo_t parentParsingInfo;
    parentParsingInfo.tokenNum = 0;
    parentParsingInfo.arg = &pMP->nvParamList.nvpList;
    sipParsingStatus_t parsingStatus;

	status = sipParserHdr_commaGenericParam(pSipMsg, hdrEndPos, &parentParsingInfo, &parsingStatus);
	if(status != OS_STATUS_OK)
	{
		goto EXIT;
	}

    pMP->nvParamList.pNVP = sipParamNV_takeTopNVfromList(&pMP->nvParamList.nvpList, &pMP->nvParamList.nvpNum);

EXIT:
    if(status != OS_STATUS_OK)
    {
        osList_clear(&pMP->nvParamList.nvpList);
    }

    return status;
}


osStatus_e sipParserHdr_valueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrValueParam_t* pVP)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pVP)
    {
        logError("NULL pointer, pSipMsg=%p, pVP=%p.", pSipMsg, pVP);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	status = sipParserHdr_nameValueList(pSipMsg, hdrEndPos, is4Multi, &pVP->nvParamList);
	if(status != OS_STATUS_OK || !pVP->nvParamList.pNVP)
	{
		logError("fails to sipParserHdr_nameValueList.");
		goto EXIT;
	}

	if(pVP->nvParamList.pNVP->value.l)
	{
		logError("expect the first parameter has only name, no value, but the decoded parameter has value.");
		status = OS_ERROR_INVALID_VALUE;
		//to-do, need to cleanup pVP->nvParamList
		goto EXIT;
	} 

	pVP->value = pVP->nvParamList.pNVP->name;
	osfree(pVP->nvParamList.pNVP);
	--pVP->nvParamList.nvpNum;

    pVP->nvParamList.pNVP = sipParamNV_takeTopNVfromList(&pVP->nvParamList.nvpList, &pVP->nvParamList.nvpNum);

EXIT:
	return status;
}


osStatus_e sipParserHdr_multiValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiValueParam_t* pMultiVP)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pMultiVP)
    {
        logError("NULL pointer, pSipMsg=%p, pMultiVP=%p.", pSipMsg, pMultiVP);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pMultiVP->vpNum = 0;
    pMultiVP->pVP = NULL;
    osList_init(&pMultiVP->vpList);

    while(pSipMsg->pos < hdrEndPos)
    {
		sipHdrValueParamDecoded_t* pVP = oszalloc(sizeof(sipHdrValueParamDecoded_t), sipHdrValueParamDecoded_cleanup);
        if(!pVP)
        {
            logError("fails to allocate sipHdrValueParamDecoded_t* pVP.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

        pVP->hdrPos.startPos = pSipMsg->pos;
        status = sipParserHdr_valueParam(pSipMsg, hdrEndPos, true, &pVP->hdrValue);
        if(status != OS_STATUS_OK)
        {
            logError("sipParserHdr_valueParam parsing error.");
            osfree(pVP);
            goto EXIT;
        }
        pVP->hdrPos.totalLen = pSipMsg->pos - pVP->hdrPos.startPos;

        if(pMultiVP->vpNum++ == 0)
        {
            pMultiVP->pVP = pVP;
        }
        else
        {
            osListElement_t* pLE = osList_append(&pMultiVP->vpList, pVP);
            if(pLE == NULL)
            {
                logError("osList_append failure.");
                osfree(pVP);
                status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                goto EXIT;
            }
        }
    }

EXIT:
    if(status != OS_STATUS_OK && status!= OS_ERROR_NULL_POINTER)
    {
        osfree(pMultiVP->pVP);
        osList_delete(&pMultiVP->vpList);
    }

    return status;
}


osStatus_e sipParserHdr_slashValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool is4Multi, sipHdrSlashValueParam_t* pSVP)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pSVP)
    {
        logError("NULL pointer, pSipMsg=%p, pSVP=%p.", pSipMsg, pSVP);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    status = sipParserHdr_nameValueList(pSipMsg, hdrEndPos, is4Multi, &pSVP->paramList);
    if(status != OS_STATUS_OK || !pSVP->paramList.pNVP)
    {
        logError("fails to sipParserHdr_nameValueList.");
        goto EXIT;
    }

    if(pSVP->paramList.pNVP->value.l)
    {
        logError("expect the first parameter has only name, no value, but the decoded parameter has value.");
        status = OS_ERROR_INVALID_VALUE;
        //to-do, need to cleanup pVP->nvParamList
        goto EXIT;
    }

    osPL_split(&pSVP->paramList.pNVP->name, '/', &pSVP->mType, &pSVP->mSubType);
	osPL_trimLWS(&pSVP->mType, true, true);
	osPL_trimLWS(&pSVP->mSubType, true, true); 
    osfree(pSVP->paramList.pNVP);
    --pSVP->paramList.nvpNum;

    pSVP->paramList.pNVP = sipParamNV_takeTopNVfromList(&pSVP->paramList.nvpList, &pSVP->paramList.nvpNum);

EXIT:
    return status;
}


osStatus_e sipParserHdr_multiSlashValueParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiSlashValueParam_t* pMultiSVP)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pMultiSVP)
    {
        logError("NULL pointer, pSipMsg=%p, pMultiSVP=%p.", pSipMsg, pMultiSVP);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pMultiSVP->svpNum = 0;
    pMultiSVP->pSVP = NULL;
    osList_init(&pMultiSVP->svpList);

    while(pSipMsg->pos < hdrEndPos)
    {
        sipHdrSlashValueParamDecoded_t* pSVP = oszalloc(sizeof(sipHdrSlashValueParamDecoded_t), sipHdrSlashValueParamDecoded_cleanup);
        if(!pSVP)
        {
            logError("fails to allocate sipHdrSlashValueParamDecoded_t* pSVP.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

        pSVP->hdrPos.startPos = pSipMsg->pos;
        status = sipParserHdr_slashValueParam(pSipMsg, hdrEndPos, true, &pSVP->hdrValue);
        if(status != OS_STATUS_OK)
        {
            logError("sipParserHdr_slashValueParam parsing error.");
            osfree(pSVP);
            goto EXIT;
        }
        pSVP->hdrPos.totalLen = pSipMsg->pos - pSVP->hdrPos.startPos;

        if(pMultiSVP->svpNum++ == 0)
        {
            pMultiSVP->pSVP = pSVP;
        }
        else
        {
            osListElement_t* pLE = osList_append(&pMultiSVP->svpList, pSVP);
            if(pLE == NULL)
            {
                logError("osList_append for pMultiSVP->svpList fails.");
                osfree(pSVP);
                status = OS_ERROR_MEMORY_ALLOC_FAILURE;
                goto EXIT;
            }
        }
    }

EXIT:
    if(status != OS_STATUS_OK && status!= OS_ERROR_NULL_POINTER)
    {
        osfree(pMultiSVP->pSVP);
        osList_delete(&pMultiSVP->svpList);
    }

    return status;
}



osStatus_e sipHdrNameValue_build(sipHdrNameValueList_t* pHdr)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pHdr)
	{
		logError("null pointer, pHdr.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	osList_init(&pHdr->nvpList);
	pHdr->nvpNum = 0;
	pHdr->pNVP = NULL;

EXIT:
	return status;
}


osStatus_e sipHdrNameValue_addParam(sipHdrNameValueList_t* pHdr, sipParamNameValue_t* pNameValue)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pNameValue)
    {
        logError("null pointer, pHdr=%p, pNameValue=%p.", pHdr, pNameValue);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pHdr->nvpNum++ == 0)
	{
		pHdr->pNVP = pNameValue;
	}
	else
	{
		osList_append(&pHdr->nvpList, pNameValue);
	}

EXIT:
	return status;
}


osStatus_e sipHdrNameValue_encode(osMBuf_t* pSipBuf, void* pHdrIn, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrNameValueList_t* pHdr = pHdrIn;
	const sipHdrName_e* pHdrCode = pData;

    if(!pSipBuf || !pHdr || !pData)
    {
        logError("null pointer, pSipBuf=%p, pHdr=%p, pData=%p.", pSipBuf, pHdr, pData);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	const char* hdrName = sipHdr_getNameByCode(*pHdrCode);
	if(!hdrName)
	{
		logError("hdrName is null for hdrCode (%d).", *pHdrCode);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osMBuf_writeStr(pSipBuf, hdrName, true);
	osMBuf_writeStr(pSipBuf, ": ", true);

	if(pHdr->pNVP)
	{
		osMBuf_writePL(pSipBuf, &pHdr->pNVP->name, true);
        if(pHdr->pNVP->value.l !=0)
        {
            osMBuf_writeU8(pSipBuf, '=', true);
            osMBuf_writePL(pSipBuf, &pHdr->pNVP->value, true);
        }
	}

    osList_t* pParamList = &pHdr->nvpList;
    osListElement_t* pParamLE = pParamList->head;
    while(pParamLE)
    {
        sipParamNameValue_t* pParam = pParamLE->data;
        osMBuf_writePL(pSipBuf, &pParam->name, true);
        if(pParam->value.l !=0)
        {
            osMBuf_writeU8(pSipBuf, '=', true);
            osMBuf_writePL(pSipBuf, &pParam->value, true);
        }

        pParamLE = pParamLE->next;
		if(pParamLE)
		{
        	osMBuf_writeU8(pSipBuf, ';', true);
    	}
	}

    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	return status;
}


osStatus_e sipHdrName_build(sipHdrNameList_t* pHdr)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr)
    {
        logError("null pointer, pHdr.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osList_init(&pHdr->nameList);
	pHdr->nNum = 0;

EXIT:
    return status;
}


osStatus_e sipHdrName_addParam(sipHdrNameList_t* pHdr, sipParamName_t* pName)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pName)
    {
        logError("null pointer, pHdr=%p, pName=%p.", pHdr, pName);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osList_append(&pHdr->nameList, pName);
	pHdr->nNum++;

EXIT:
    return status;
}


osStatus_e sipHdrName_encode(osMBuf_t* pSipBuf, void* pHdrIn, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
    sipHdrNameList_t* pHdr = pHdrIn;
    const sipHdrName_e* pHdrCode = pData;

    if(!pSipBuf || !pHdr || !pData)
    {
        logError("null pointer, pSipBuf=%p, pHdr=%p, pData=%p.", pSipBuf, pHdr, pData);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    const char* hdrName = sipHdr_getNameByCode(*pHdrCode);
    if(!hdrName)
    {
        logError("hdrName is null for hdrCode (%d).", *pHdrCode);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
	}

    osMBuf_writeStr(pSipBuf, hdrName, true);
    osMBuf_writeStr(pSipBuf, ": ", true);

    osList_t* pParamList = &pHdr->nameList;
    osListElement_t* pParamLE = pParamList->head;
    while(pParamLE)
    {
        sipParamNameValue_t* pParam = pParamLE->data;
        osMBuf_writePL(pSipBuf, &pParam->name, true);

        pParamLE = pParamLE->next;
        if(pParamLE)
        {
            osMBuf_writeU8(pSipBuf, ',', true);
        }
    }

    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
    if(pHdr)
    {
        osList_clear(&pHdr->nameList);
    }

    return status;
}


osPointerLen_t* sipHdrNameValueList_getValue(sipHdrNameValueList_t* pnvList, osPointerLen_t* pName)
{
	if(!pnvList || !pName)
	{
		logError("null pointer, pnvList=%p, pName=%p.", pnvList, pName);
		return NULL;
	}

	if(pnvList->nvpNum <=0)
	{
		return NULL;
	}

	if(osPL_casecmp(&pnvList->pNVP->name, pName) == 0)
	{
		return &pnvList->pNVP->value;
	}

	osListElement_t* pLE = pnvList->nvpList.head;
	if(pLE)
	{
		sipParamNameValue_t* pNV = pLE->data;
		if(pNV && osPL_casecmp(&pNV->name, pName) == 0)
		{
			return &pNV->value;
		}

		pLE = pLE->next;
	}

	return NULL;
}
