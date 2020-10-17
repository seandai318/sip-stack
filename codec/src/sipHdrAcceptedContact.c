/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrAcceptedContact.c
 ********************************************************/

#include "osDebug.h"
#include "osList.h"
#include "osMemory.h"
#include "sipHdrAcceptedContact.h"
#include "sipGenericNameParam.h"


osStatus_e sipHdr_acceptedContact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrAcceptedContact_t* pAC)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pAC)
    {
        logError("NULL pointer, pSipMsg=%p, pAC=%p.", pSipMsg, pAC);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

//viaList shall be initialized out of this function, otherwise, multiple via hdr will initiate the viaList each other
//    osList_init(&pVia->viaList);

    while(pSipMsg->pos < hdrEndPos)
    {
        osList_t* pACElem = osmalloc(sizeof(osList_t), NULL);
        if(!pACElem)
        {
            logError("could not allocate memory for pACElem.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

		osList_init(pACElem);

	    sipParsingInfo_t parentParsingInfo;
    	parentParsingInfo.arg = pACElem;
	    parentParsingInfo.tokenNum = 1;
    	parentParsingInfo.token[0]=',';
	    sipParsingStatus_t parsingStatus;

		status = sipParserHdr_genericParam(pSipMsg, hdrEndPos, &parentParsingInfo, &parsingStatus);		
        if(status != OS_STATUS_OK)
        {
            logError("accepted contact parsing error.")
            goto EXIT;
        }

		osListElement_t* pLEac = pACElem->head;
		if(pLEac)
		{
			sipHdrParamNameValue_t* pLEacData = pLEac->data;
			if(pLEacData->name.l != 1 || *((char*) pLEacData->name.p) != '*'||pLEacData->value.l !=0)
			{
				logError("the first parameter of accept contact shall be '*', but now it is '%c', value.l=%ld.", *((char*) pLEacData->name.p), pLEacData->value.l);
				status =OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
		}

        osListElement_t* pLE = osList_append(&pAC->acList, pACElem);
        if(pLE == NULL)
        {
            logError("osList_append failure for accept contact.");
            osfree(pACElem);
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
		//TODO: also need to delete the list inside acList
        osList_delete(&pAC->acList);
    }

	return status;
}


void sipHdrAC_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    osList_t* pACList = &((sipHdrAcceptedContact_t*)data)->acList;

    osListElement_t* pACLE = pACList->head;
    while(pACLE)
    {
        osList_t* pGPList = pACLE->data;
        osList_delete(pGPList);
        pACLE = pACLE->next;
    }
}


void* sipHdrAC_alloc()
{
	sipHdrAcceptedContact_t* pAC = osmalloc(sizeof(sipHdrAcceptedContact_t), sipHdrAC_cleanup);

	if(!pAC)
	{
		return NULL;
	}

	osList_init(&pAC->acList);

	return pAC;
}
