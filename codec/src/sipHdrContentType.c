/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrContentType.c
 ********************************************************/

#include "osDebug.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "osMemory.h"
#include "sipHdrContentType.h"
#include "sipGenericNameParam.h"


static osStatus_e sipParsing_setCTypeParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);
static sipParsingABNF_t sipCTypeABNF[]={  \
	{1, 1, 					SIP_TOKEN_INVALID, 	0, SIPP_PARAM_MEDIA, 		 sipParsing_plGetParam, 	NULL},
	{1, 1,					'/'				 ,  0, SIPP_PARAM_SUBMEDIA,     sipParsing_plGetParam,     NULL},    
    {0, SIPP_MAX_PARAM_NUM, ';', 				0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParserHdr_genericParam, NULL}};
static int sipP_cTypeNum = sizeof(sipCTypeABNF)/sizeof(sipParsingABNF_t);



osStatus_e sipParserHdr_contentType(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrContentType_t* pCType)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_cTypeNum];
    SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_cTypeNum);

    if(!pSipMsg || !pCType)
    {
        logError("NULL pointer, pSipMsg=%p, pCType=%p.", pSipMsg, pCType);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipParsingInfo_t parentParsingInfo;
    parentParsingInfo.arg = pCType;
    parentParsingInfo.tokenNum = 0;
    sipParsingStatus_t parsingStatus;
    sipParsing_setParsingInfo(sipCTypeABNF, sipP_cTypeNum, &parentParsingInfo, sippParsingInfo, sipParsing_setCTypeParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipCTypeABNF, sippParsingInfo, sipP_cTypeNum, &parsingStatus);

    if(status != OS_STATUS_OK)
    {
        logError("Content Type parsing error.")
        goto EXIT;
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
        osList_delete(&pCType->mParamList);
    }

    DEBUG_END
    return status;
}


static osStatus_e sipParsing_setCTypeParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    sipHdrContentType_t* pCType = (sipHdrContentType_t*) arg;

    switch (paramName)
    {
        case SIPP_PARAM_MEDIA:
            pSippParsingInfo->arg = &pCType->media;
            break;

		case SIPP_PARAM_SUBMEDIA:
			pSippParsingInfo->arg = &pCType->subMedia;
			break;

        case SIPP_PARAM_GENERIC_NAMEPARAM:
            osList_init(&pCType->mParamList);
            pSippParsingInfo->arg = &pCType->mParamList;
            break;

        default:
            logError("unexpected parameter for Content Type parameter, sipCTypeABNF.paramName=%s.", paramName);
            status = OS_ERROR_INVALID_VALUE;
    }

    return status;
}


void* sipHdrContentType_alloc()
{
	sipHdrContentType_t* pCType = osmalloc(sizeof(sipHdrContentType_t), sipHdrContentType_cleanup);
	if(!pCType)
	{
		return NULL;
	}

	osList_init(&pCType->mParamList);
	
	return pCType;
}

	
void sipHdrContentType_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdrContentType_t* pCType = data;
	osList_delete(&pCType->mParamList);
}
