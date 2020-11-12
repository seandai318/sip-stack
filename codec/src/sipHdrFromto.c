/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrFromto.c
 ********************************************************/

#include "osDebug.h"
#include "osMisc.h"
#include "osList.h"
#include "osMemory.h"

#include "sipConfig.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "sipHdrFromto.h"



osStatus_e sipParserHdr_fromto(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdr_fromto_t* pFromto)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pFromto)
    {
        logError("NULL pointer, pSipMsg=%p, pFromto=%p.", pSipMsg, pFromto);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osList_init(&pFromto->fromto.genericParam);
    status = sipParserHdr_genericNameParam(pSipMsg, hdrEndPos, false, &pFromto->fromto);
    if(status != OS_STATUS_OK)
    {
        logError("Fromto parsing error.")
        goto EXIT;
    }

	if(pSipMsg->pos < hdrEndPos)
	{
		logError("From/To header has more characters than expected, parsing end pos=%d, hdrEndPos=%d.", pSipMsg->pos, hdrEndPos);
		goto EXIT;
	}

EXIT:
    DEBUG_END
    return status;
}


osStatus_e sipHdrFrom_encode(osMBuf_t* pSipBuf, void* pFromDT, void* pTagDT)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	sipUriExt_t* pFrom = pFromDT;
	osPointerLen_t* pTag = pTagDT;

    if(!pSipBuf || !pFrom || !pTag)
    {
        logError("Null pointer, pSipBuf=%p, pFrom=%p, pTag=%p.", pSipBuf, pFrom, pTag);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pFrom->displayName.l != 0)
	{
		osMBuf_writeStr(pSipBuf, "From: \"", true);
		osMBuf_writePL(pSipBuf, &pFrom->displayName, true);
		osMBuf_writeStr(pSipBuf, "\" <", true);
	}
	else
	{
		osMBuf_writeStr(pSipBuf, "From: <", true);
	}

	sipParamUri_encode(pSipBuf, &pFrom->uri);
	osMBuf_writeStr(pSipBuf, ">;tag=", true);
	osMBuf_writePL(pSipBuf, pTag, true);
	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipHdrFrom_create(void* pFromHdrDT, void* pFromUriExtDT, void* pFromTagDT)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;
	
	sipUriExt_t* pFromHdr = pFromHdrDT;
	sipUriExt_t* pFromUri = pFromUriExtDT;
	osPointerLen_t* pFromTag = pFromTagDT;

	if(!pFromHdr || !pFromUri || !pFromTag)
	{
		logError("null pointer, pFromHdr=%p, pFromUri=%p, pFromTag=%p.", pFromHdr, pFromUri, pFromTag);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    status = sipHdrFromto_generateTagId(pFromTag, false);
    if(status != OS_STATUS_OK)
    {
        logError("generate tagId fails.");
        goto EXIT;
    }

	//pFromHdr shall have been assigned fromUri
EXIT:
	DEBUG_END
	return status;
}


osStatus_e sipHdrTo_encode(osMBuf_t* pSipBuf, void* pToDT, void* other)
{
	DEBUG_BEGIN
    osStatus_e status = OS_STATUS_OK;
	sipUriExt_t* pTo = pToDT;

    if(!pSipBuf || !pTo)
    {
        logError("Null pointer, pSipBuf=%p, pTo=%p.", pSipBuf, pTo);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(pTo->displayName.l != 0)
    {
        osMBuf_writeStr(pSipBuf, "To: \"", true);
        osMBuf_writePL(pSipBuf, &pTo->displayName, true);
        osMBuf_writeStr(pSipBuf, "\" <", true);
    }
    else
    {
        osMBuf_writeStr(pSipBuf, "To: <", true);
    }

    sipParamUri_encode(pSipBuf, &pTo->uri);
    osMBuf_writeStr(pSipBuf, ">\r\n", true);

EXIT:
	DEBUG_END
    return status;
}


osStatus_e sipHdrTo_create(void* pToHdrDT, void* pToUriExtDT, void* other)
{
    osStatus_e status = OS_STATUS_OK;

    sipUriExt_t* pToHdr = pToHdrDT;
    sipUriExt_t* pToUri = pToUriExtDT;

    if(!pToHdr || !pToUri)
    {
        logError("null pointer, pToHdr=%p, pToUri=%p.", pToHdr, pToUri);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    //pToHdr shall have been assigned ToUri
EXIT:
    return status;
}


osStatus_e sipHdrFromto_generateTagId(osPointerLen_t* pTagId, bool isTagLabel)
{
	osStatus_e status = OS_STATUS_OK;
	char* tagId = NULL;

	if(!pTagId)
	{
		logError("null pointer, pTagId.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}
	pTagId->p = NULL;

    tagId = osmalloc(SIP_MAX_TAG_ID_LENGTH, NULL);
    if(tagId == NULL)
    {
        logError("allocate tagId fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    srand(tp.tv_nsec);
    int randValue=rand();

	if(isTagLabel)
	{
		pTagId->l = sprintf(tagId, "tag=%lx%lx%s", (tp.tv_nsec+randValue), tp.tv_sec %100000, osGetNodeId());
	}
	else
	{
		pTagId->l = sprintf(tagId, "%lx%lx%s", (tp.tv_nsec+randValue), tp.tv_sec %100000, osGetNodeId());
	}

	if(pTagId->l >= SIP_MAX_TAG_ID_LENGTH)
	{
		logError("the tagId length (%d)  exceeds the maximum allowable length.", pTagId->l);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
	}

	pTagId->p = tagId;
//	pTagId->pIsDynamic = true;
	
EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(tagId);
		pTagId->p = NULL;
		pTagId->l = 0;
	}

	return status;
}


osStatus_e sipHdrFromto_generateSipPLTagId(sipPointerLen_t* pTagId, bool isTagLabel)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pTagId)
    {
        logError("null pointer, pTagId.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    srand(tp.tv_nsec);
    int randValue=rand();

    if(isTagLabel)
    {
        pTagId->pl.l = sprintf((char*)pTagId->pl.p, "tag=%lx%lx%s", (tp.tv_nsec+randValue), tp.tv_sec %100000, osGetNodeId());
    }
    else
    {
        pTagId->pl.l = sprintf((char*)pTagId->pl.p, "%lx%lx%s", (tp.tv_nsec+randValue), tp.tv_sec %100000, osGetNodeId());
    }

    if(pTagId->pl.l >= SIP_MAX_TAG_ID_LENGTH)
    {
        logError("the tagId length (%d)  exceeds the maximum allowable length.", pTagId->pl.l);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
        pTagId->pl.l = 0;
    }

    return status;
}


void sipHdrFromto_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdr_fromto_t* pFromto = data;
	sipHdrGenericNameParam_cleanup(&pFromto->fromto);
}


void* sipHdrFromto_alloc()
{
	return osmalloc(sizeof(sipHdr_fromto_t), sipHdrFromto_cleanup);
}
