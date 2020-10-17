/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrRoute.c
 ********************************************************/

#include "osList.h"
#include "osMemory.h"

#include "sipHdrRoute.h"
#include "sipHeader.h"


sipParserHdr_multiNameParam_h sipParserHdr_route = sipParserHdr_multiNameParam;

#if 0
osStatus_e sipHdrRoute_encode(osMBuf_t* pSipBuf, void* pRouteDT, void* pData)
{
	osStatus_e status = OS_STATUS_OK;
	sipHdrRouteElementPT_t* pRoute = pRouteDT;

	if(!pSipBuf || !pRouteDT || !pData)
	{
		logError("null pointer, pSipBuf=%p, pRouteDT=%p, pData=%p.", pSipBuf, pRouteDT, pData);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipHdrName_e* pHdrName = pData;


	if(osList_getCount(&pRoute->genericParam) != 0)
	{
		logError("Route or Record-Route contains generic parameter.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(*pHdrName == SIP_HDR_ROUTE)
	{
    	osMBuf_writeStr(pSipBuf, "Route: <", true);
	}
	else if(*pHdrName == SIP_HDR_RECORD_ROUTE)
	{
		osMBuf_writeStr(pSipBuf, "Record-Route: <", true);
	}
	else
	{
		logError("incorrect hdrName (%d).", *pHdrName);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

    status = sipParamUri_encode(pSipBuf, pRoute->pUri);
	if(status != OS_STATUS_OK)
	{
		logError("sipHdrParamUri.encode faile.");
		goto EXIT;
	}

    osMBuf_writeU8(pSipBuf, '>', true);

    osList_t* pParamList = &pRoute->genericParam;
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
    return status;
}
#endif	


//if pUri is not provided, this function will create one, otherwise, just use the uri passed in)
osStatus_e sipHdrRoute_create(void* pRouteDT, void* pUriDT, void* other)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrGenericNameParamPt_t* pRoute = pRouteDT;
	sipUri_t* pUri = pUriDT;

    if(!pRoute || !pUri)
    {
        logError("null pointer, pRoute=%p, pUri=%p.", pRoute, pUri);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pUri->hostport.host.l == 0)
	{
		status = sipParamUri_create(pRoute->pUri);
		if(status != OS_STATUS_OK)
		{
			logError("sipHdrParamUri.create() fails.");
			goto EXIT;
		}

		if(pUri->uriParam.uriParamMask == 0)
		{
			pUri->uriParam.uriParamMask = 1<<SIP_URI_PARAM_LR;
		}
	}
	else
	{
		pRoute->pUri = pUriDT;
	}

EXIT:
    return status;
}


osStatus_e sipHdrRoute_build(sipHdrRouteElementPT_t* pRoute, sipUri_t* pUri, osPointerLen_t* displayname)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pRoute || !pUri)
	{
		logError("null pointer, pRoute=%p, pUri=%p.", pRoute, pUri);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pRoute->pUri = pUri;
	
	if(displayname)
	{
		pRoute->displayName = *displayname;
	}
	else
	{
		pRoute->displayName.l = 0;
	}

	osList_init(&pRoute->genericParam);

EXIT:
	return status;
}


osStatus_e sipHdrRoute_addParam(sipHdrRouteElementPT_t* pRoute, sipHdrParamNameValue_t* pNameValue)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pRoute || !pNameValue)
    {
        logError("null pointer, pRoute=%p, pNameValue=%p.", pRoute, pNameValue);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osList_append(&pRoute->genericParam, pNameValue);

EXIT:
	return status;
}


#if 0
osStatus_e sipHdrRoute_createTopRouteModifyInfo(sipHdrModifyInfo_t* pRouteModify, sipUri_t* pUri, sipHdrName_e* pHdrName)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pRouteModify || !pHdrName)
    {
        logError("null pointer, pRouteModify=%p, pHdrName=%p.", pRouteModify, pHdrName);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    if(!pRouteModify->pOrigSipDecoded)
    {
        logError("null pointer, pRouteModify->pOrigSipDecoded.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    status = sipHdrRoute_getTopPosLen(pRouteModify->pOrigSipDecoded, &pRouteModify->origHdrpos, &pRouteModify->origHdrLen, *pHdrName==SIP_HDR_RECORD_ROUTE ? true:false);
    if(status != OS_STATUS_OK)
    {
        logError("sipHdrRoute_getTopRoutePos() fails.");
        goto EXIT;
    }

	if(pRouteModify->origHdrpos == 0)
	{
	    if(*pData==SIP_HDR_RECORD_ROUTE)
		{
    		status = sipHdrCallId_getPosLen(pRouteModify->pOrigSipDecoded, &pRouteModify->origHdrpos, &pRouteModify->origHdrLen);
    		if(status != OS_STATUS_OK || pRouteModify->origHdrpos == 0)
    		{
        		logError("sipHdrCallId_getCallIdPos() fails.");
                status = !pRouteModify->origHdrpos ? OS_ERROR_INVALID_VALUE : status;
        		goto EXIT;
    		}
		}
		else
		{
			pRouteModify->sipHdrEncode_handler = NULL;
			pRouteModify->origHdrLen = 0;
			goto EXIT;
		}
	}

	pRouteModify->pData = pHdrName;
	switch(pRouteModify->modType)
	{
		case SIP_HDR_MODIFY_TYPE_ADD:	
			status = sipHdrRoute_create(pRouteModify->pHdr, pUri);
		    if(status != OS_STATUS_OK)
    		{
        		logError("sipHdrRoute_create() fails.");
        		goto EXIT;
    		}

			//override the len.
            pRouteModify->origHdrLen = 0;
    		pRouteModify->sipHdrEncode_handler = sipHdrRoute_encode;
			break;
		case SIP_HDR_MODIFY_TYPE_REMOVE:
			pRouteModify->sipHdrEncode_handler = NULL;
			break;
		case SIP_HDR_MODIFY_TYPE_REPLACE:
            status = sipHdrRoute_create(pRouteModify->pHdr, pUri);
            if(status != OS_STATUS_OK)
            {
                logError("sipHdrRoute_create() fails.");
                goto EXIT;
            }

			pRouteModify->sipHdrEncode_handler = sipHdrRoute_encode;
			break;
		default:
			logError("pRouteModify->modType(%d) is not handled.", pRouteModify->modType);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
	}

EXIT:
    return status;
}
#endif

	
void sipHdrRoute_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipHdrMultiGenericNameParam_cleanup(data);
}


void* sipHdrRoute_alloc()
{
    sipHdrRoute_t* pRoute = oszalloc(sizeof(sipHdrRoute_t), sipHdrRoute_cleanup);
    if(!pRoute)
    {
        return NULL;
    }

    return pRoute;
}

