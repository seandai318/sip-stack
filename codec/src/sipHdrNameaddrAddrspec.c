/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrNameaddrAddrspec.c
 ********************************************************/

#include "osList.h"
#include "osMemory.h"

#include "sipHdrNameaddrAddrspec.h"
#include "sipGenericNameParam.h"


osStatus_e sipParserHdr_nameaddrAddrSpec(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrNameaddrAddrspec_t* pAddr)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pAddr)
	{
		logError("pAddr is NULL.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	status = sipParserHdr_multiNameParam(pSipMsg, hdrEndPos, false, pAddr);
	if(status != OS_STATUS_OK)
	{
		logError("sipParserHdr_nameaddrAddrSpec error.");
		goto EXIT;
	}

	if(osList_getCount(&pAddr->pGNP->hdrValue.genericParam) !=0)
	{
		logError("error, the first pGNP of the parsed header has parameter(s) other than nameaddr or addrspec,");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	osList_t* pAddrList = &pAddr->gnpList;
	osListElement_t* pAddrLE = pAddrList->head;
	while(pAddrLE)
	{
		sipHdrGenericNameParamDecoded_t* pNameParam = pAddrLE->data;
		if(!pNameParam)
		{
			logError("list for sipHdrGenericNameParam_t has null data.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		if(osList_getCount(&pNameParam->hdrValue.genericParam) !=0)
		{
			logError("error, the 2nd and forward pGNP of the parsed header has parameter(s) other than nameaddr or addrspec,"); 
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		pAddrLE = pAddrLE->next;
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pAddr);
	}

	return status;
}



void sipHdrNameaddrAddrspec_dealloc(void* data)
{
	osfree(data);
}
