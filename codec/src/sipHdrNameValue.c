
#include "osDebug.h"
#include "osList.h"

#include "sipHdrNameValue.h"
#include "sipHeader.h"


osStatus_e sipHdrNameValue_build(sipHdr_nameValue_t* pHdr)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pHdr)
	{
		logError("null pointer, pHdr.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	osList_init(&pHdr->nvParam);

EXIT:
	return status;
}


osStatus_e sipHdrNameValue_addParam(sipHdr_nameValue_t* pHdr, sipParamNameValue_t* pNameValue)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pNameValue)
    {
        logError("null pointer, pHdr=%p, pNameValue=%p.", pHdr, pNameValue);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osList_append(&pHdr->nvParam, pNameValue);

EXIT:
	return status;
}

osStatus_e sipHdrNameValue_encode(osMBuf_t* pSipBuf, void* pHdrIn, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdr_nameValue_t* pHdr = pHdrIn;
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

    osList_t* pParamList = &pHdr->nvParam;
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
	if(pHdr)
	{
		osList_clear(&pHdr->nvParam);
	}
	return status;
}


osStatus_e sipHdrName_build(sipHdr_name_t* pHdr)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr)
    {
        logError("null pointer, pHdr.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osList_init(&pHdr->nParam);

EXIT:
    return status;
}


osStatus_e sipHdrName_addParam(sipHdr_name_t* pHdr, sipParamName_t* pName)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pHdr || !pName)
    {
        logError("null pointer, pHdr=%p, pName=%p.", pHdr, pName);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    osList_append(&pHdr->nParam, pName);

EXIT:
    return status;
}


osStatus_e sipHdrName_encode(osMBuf_t* pSipBuf, void* pHdrIn, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
    sipHdr_name_t* pHdr = pHdrIn;
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

    osList_t* pParamList = &pHdr->nParam;
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
        osList_clear(&pHdr->nParam);
    }

    return status;
}
