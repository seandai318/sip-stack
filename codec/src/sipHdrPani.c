/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrPani.c
 ********************************************************/

#include "osDebug.h"
#include "osList.h"
#include "osMemory.h"
#include "sipHdrPani.h"
#include "sipGenericNameParam.h"


osStatus_e sipHdr_pani(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrPani_t* pPani)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pPani)
    {
        logError("NULL pointer, pSipMsg=%p, pPani=%p.", pSipMsg, pPani);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

//viaList shall be initialized out of this function, otherwise, multiple via hdr will initiate the viaList each other
//    osList_init(&pVia->viaList);

    while(pSipMsg->pos < hdrEndPos)
    {
        osList_t* pPaniElem = osmalloc(sizeof(osList_t), NULL);
        if(!pPaniElem)
        {
            logError("could not allocate memory for pPaniElem.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

		osList_init(pPaniElem);

	    sipParsingInfo_t parentParsingInfo;
    	parentParsingInfo.arg = pPaniElem;
	    parentParsingInfo.tokenNum = 1;
    	parentParsingInfo.token[0]=',';
	    sipParsingStatus_t parsingStatus;

		status = sipParserHdr_genericParam(pSipMsg, hdrEndPos, &parentParsingInfo, &parsingStatus);		
        if(status != OS_STATUS_OK)
        {
            logError("accepted contact parsing error.")
            goto EXIT;
        }

        osListElement_t* pLEpani = pPaniElem->head;
        if(pLEpani)
        {
            sipHdrParamNameValue_t* pLEpaniData = pLEpani->data;
            if(pLEpaniData->value.l != 0)
            {
                logError("the first parameter of PANI shall only has name, but now it has value too, value.l=%ld.", pLEpaniData->value.l);
                status =OS_ERROR_INVALID_VALUE;
                goto EXIT;
            }
        }

        osListElement_t* pLE = osList_append(&pPani->paniList, pPaniElem);
        if(pLE == NULL)
        {
            logError("osList_append failure for accept contact.");
            osfree(pPaniElem);
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
		//TODO: also need to delete the list inside paniList
        osList_delete(&pPani->paniList);
    }

	return status;
}


void sipHdrPani_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	osList_t* pPaniList = &((sipHdrPani_t*)data)->paniList;

	osListElement_t* pPaniLE = pPaniList->head;
	while(pPaniLE)
	{
		osList_t* pGPList = pPaniLE->data;
		osList_delete(pGPList);
		pPaniLE = pPaniLE->next;
	}
}


void* sipHdrPani_alloc()
{
	sipHdrPani_t* pPani = osmalloc(sizeof(sipHdrPani_t), sipHdrPani_cleanup);
	if(!pPani)
	{
		return NULL;
	}

	osList_init(&pPani->paniList);

	return pPani;
}
