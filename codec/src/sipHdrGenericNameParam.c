
#include "osDebug.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "osMemory.h"
#include "sipHeader.h"


//display name could not have ':' except inside double quote.
/* algorithm to check if there is display name:
 * the hdr value already has the leading LWS filtered.  If the 1st char is '"', there is display name
 * otherwise, if parsing meets '<' before ':', there is display name
 * otherwise, no display name
 */

static sipParsingABNF_t sipGenericParamABNF[]={  \
    {0, SIPP_MAX_PARAM_NUM, SIP_TOKEN_INVALID, 1, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL}, \
    {0, 1,                  ';',               0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL},
	{0, 1,					SIP_TOKEN_EOH,	   0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParsing_listPLGetParam, NULL}};


static int sipP_genericParamNum = sizeof(sipGenericParamABNF) / sizeof(sipParsingABNF_t);


static osStatus_e sipParsing_setGenericParamParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);


osStatus_e sipParserHdr_genericNameParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddrOnly, sipHdrGenericNameParam_t* pNameParam)
{
	DEBUG_BEGIN

	osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_genericParamNum];
    SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_genericParamNum);

	if(!pSipMsg || !pNameParam)
	{
		logError("NULL pointer, pSipMsg=%p, pNameParam=%p.", pSipMsg, pNameParam);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pNameParam->displayName.l = 0;

	bool isDisplayName = false;
	bool isRAQ = false;
	if(pSipMsg->buf[pSipMsg->pos] == '"')
	{
		isDisplayName = true;
		isRAQ = true;
	}
	else if(pSipMsg->buf[pSipMsg->pos] == '<')
	{
		isRAQ = true;
	}

	size_t origPos = pSipMsg->pos;
	if(!isDisplayName && pSipMsg->buf[origPos] != '<')
	{
		while(pSipMsg->pos < hdrEndPos)
		{
			if(pSipMsg->buf[pSipMsg->pos] == ':')
			{
				//there is no display name, you are done.
				break;
			}
		
			if(pSipMsg->buf[pSipMsg->pos] == '<')
			{
				//there is display name, you are done.
				isDisplayName = true;
				isRAQ = true;
				break;
			}

			pSipMsg->pos++;
		}

		if(isNameaddrOnly && !isDisplayName)
		{
			logError("not a name-addr parameter, but expect name-addr.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}
	}

	if(pSipMsg->pos == hdrEndPos)
	{
		logError("could not determine whether there is display name.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pSipMsg->pos = origPos;
	if(isDisplayName)
	{
		while(pSipMsg->pos < hdrEndPos)
		{
			if(pSipMsg->buf[pSipMsg->pos] == '<')
			{
				pNameParam->displayName.p = &pSipMsg->buf[origPos];
				pNameParam->displayName.l = pSipMsg->pos - origPos;
				for(int i=pSipMsg->pos-1; i>origPos; i--)
				{
					if(SIP_IS_LWS(pSipMsg->buf[i]))
					{
						pNameParam->displayName.l--;
					}
					else
					{
						break;
					}
				}

				pSipMsg->pos++;
				break;
			}
				
			pSipMsg->pos++;
		}
	}
	else if(pSipMsg->buf[pSipMsg->pos] == '<')
	{
		//need to start the next level parsing after '<'
	    pSipMsg->pos++;
	}

	//start parsing the URI of contact header
	origPos = pSipMsg->pos;
    sipUri_t* pUri = &pNameParam->uri;
    osList_init(&pUri->headers);
    sipParsingStatus_t parsingStatus;
    sipParsingInfo_t parentParsingInfo;
    parentParsingInfo.arg = pUri;
	if(isRAQ)
	{
    	parentParsingInfo.token[0]='>';
	}
	else
	{
		parentParsingInfo.token[0]=';';
	}
    parentParsingInfo.extTokenNum=0;
    parentParsingInfo.tokenNum = 1;
    status = sipParamUri_parse(pSipMsg, hdrEndPos, &parentParsingInfo, &parsingStatus);
	if(status != OS_STATUS_OK)
	{
		logError("parsing contact URI failure, status=%d.", status);
		goto EXIT;
	}

	//if no more parameters, we are done
	if(parsingStatus.isEOH)
	{
		goto EXIT;
	}

	//start parsing other contact parameters
	parentParsingInfo.tokenNum = 1;
	parentParsingInfo.token[0]=',';
//	parentParsingInfo.tokenNum = 0;
	parentParsingInfo.arg = &pNameParam->genericParam;

    sipParsing_setParsingInfo(sipGenericParamABNF, sipP_genericParamNum, &parentParsingInfo, sippParsingInfo, sipParsing_setGenericParamParsingInfo);

	//debug to remove
#if 1
	debug("sean, pos=%ld, hdrPos=%ld, isEOH=%d.", pSipMsg->pos, hdrEndPos, parsingStatus.isEOH);
	for(int i=0; i<2; i++)
	{
		debug("sean, i=%d, sippParsingInfo[i].tokenNum=%d", i, sippParsingInfo[i].tokenNum);
	}
#endif
    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipGenericParamABNF, sippParsingInfo, sipP_genericParamNum, &parsingStatus);

EXIT:
	return status;
}


osStatus_e sipParserHdr_multiNameParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddrOnly, osList_t* pNameParamList)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pNameParamList)
    {
        logError("NULL pointer, pSipMsg=%p, pNameParamList=%p.", pSipMsg, pNameParamList);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

//	pNameParamList shall be initialized one time, and out of this function
//	osList_init(pNameParamList);

    while(pSipMsg->pos < hdrEndPos)
    {
        sipHdrGenericNameParam_t* pNameParam = osMem_alloc(sizeof(sipHdrGenericNameParam_t), NULL);
        if(!pNameParam)
        {
            logError("could not allocate memory for pNameParam.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

        osList_init(&pNameParam->genericParam);

        status = sipParserHdr_genericNameParam(pSipMsg, hdrEndPos, isNameaddrOnly, pNameParam);
        if(status != OS_STATUS_OK)
        {
            logError("sipParserHdr_genericNameParam parsing error.")
            goto EXIT;
        }

        osListElement_t* pLE = osList_append(pNameParamList, pNameParam);
        if(pLE == NULL)
        {
            logError("osList_append failure.");
            osMem_deref(pNameParam);
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
        osList_delete(pNameParamList);
    }

    DEBUG_END
    return status;
}


//this function parses *(;name=value)
osStatus_e sipParserHdr_genericParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pStatus)
{
	osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_genericParamNum];
    SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_genericParamNum);

    if(!pSipMsg || !pParentParsingInfo || !pStatus)
    {
        logError("NULL pointer, pSipMsg=%p, pParentParsingInfo=%p, pStatus=%p.", pSipMsg, pParentParsingInfo, pStatus);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }
	
    sipParsing_setParsingInfo(sipGenericParamABNF, sipP_genericParamNum, pParentParsingInfo, sippParsingInfo, sipParsing_setGenericParamParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipGenericParamABNF, sippParsingInfo, sipP_genericParamNum, pStatus);

EXIT:
	return status;
}


osStatus_e sipHdrGenericNameParam_build(sipHdrGenericNameParamPt_t* pHdr, sipUri_t* pUri, osPointerLen_t* displayname)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pUri)
    {
        logError("null pointer, pHdr=%p, pUri=%p.", pHdr, pUri);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    pHdr->pUri = pUri;

    if(displayname)
    {
        pHdr->displayName = *displayname;
    }
    else
    {
        pHdr->displayName.l = 0;
    }

    osList_init(&pHdr->genericParam);

EXIT:
    return status;
}


osStatus_e sipHdrGenericNameParam_addParam(sipHdrGenericNameParamPt_t* pHdr, sipHdrParamNameValue_t* pNameValue)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pNameValue)
    {
        logError("null pointer, pHdr=%p, pNameValue=%p.", pHdr, pNameValue);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osList_append(&pHdr->genericParam, pNameValue);

EXIT:
    return status;
}


osStatus_e sipHdrGenericNameParam_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
    sipHdrGenericNameParamPt_t* pHdr = pHdrDT;

    if(!pSipBuf || !pHdrDT || !pData)
    {
        logError("null pointer, pSipBuf=%p, pHdrDT=%p, pData=%p.", pSipBuf, pHdrDT, pData);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    const char* hdrName = sipHdr_getNameByCode(*(sipHdrName_e*)pData);
    if(!hdrName)
    {
        logError("hdrName is null for hdrCode (%d).", *(sipHdrName_e*)pData);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    osMBuf_writeStr(pSipBuf, hdrName, true);
    osMBuf_writeStr(pSipBuf, ": ", true);

	if(pHdr->displayName.l > 0)
	{
		osMBuf_writePL(pSipBuf, &pHdr->displayName, true);
		osMBuf_writeU8(pSipBuf, ' ', true);
	}

	osMBuf_writeU8(pSipBuf, '<', true);

    status = sipParamUri_encode(pSipBuf, pHdr->pUri);
    if(status != OS_STATUS_OK)
    {
        logError("sipHdrParamUri_encode fails.");
        goto EXIT;
    }

    osMBuf_writeU8(pSipBuf, '>', true);

    osList_t* pParamList = &pHdr->genericParam;
    osListElement_t* pParamLE = pParamList->head;
    while(pParamLE)
    {
        osMBuf_writeU8(pSipBuf, ';', true);
        sipHdrParamNameValue_t* pParam = pParamLE->data;
        osMBuf_writePL(pSipBuf, &pParam->name, true);
        if(pParam->value.l !=0)
        {
            osMBuf_writeU8(pSipBuf, '=', true);
            osMBuf_writePL(pSipBuf, &pParam->value, true);
        }

        pParamLE = pParamLE->next;
    }

    osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	if(pHdr)
	{
		osList_clear(&pHdr->genericParam);
		if(pHdr->pUri)
		{
			sipParamUri_clear(pHdr->pUri);
		}
	}
    return status;
}


static osStatus_e sipParsing_setGenericParamParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    switch (paramName)
    {
        case SIPP_PARAM_GENERIC_NAMEPARAM:
            pSippParsingInfo->arg = arg;
            break;

        default:
            logError("unexpected parameter, sipUserinfoABNF paramName=%s.", paramName);
            status = OS_ERROR_INVALID_VALUE;
            goto EXIT;
    }

EXIT:
    return status;
}


void sipHdrGenericNameParam_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdrGenericNameParam_t* pGNP = data;
    sipUri_cleanup(&pGNP->uri);
    osList_delete(&pGNP->genericParam);
}


void* sipHdrGenericNameParam_alloc()
{
	sipHdrGenericNameParam_t* pGNP = osMem_alloc(sizeof(sipHdrGenericNameParam_t), sipHdrGenericNameParam_cleanup);
	if(!pGNP)
	{
		return NULL;
	}

	osList_init(&pGNP->genericParam);

	return pGNP;
}
