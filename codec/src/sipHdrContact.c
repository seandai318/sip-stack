
#include "osDebug.h"
#include "osMemory.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "sipHdrContact.h"


//display name could not have ':' except inside double quote.
/* algorithm to check if there is display name:
 * the hdr value already has the leading LWS filtered.  If the 1st char is '"', there is display name
 * otherwise, if parsing meets '<' before ':', there is display name
 * otherwise, no display name
 */




osStatus_e sipParserHdr_contact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrContact_t* pContact)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pContact)
    {
        logError("NULL pointer, pSipMsg=%p, pContact=%p.", pSipMsg, pContact);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//contactList and isStar shall be initiated outside of this function

	if(pContact->isStar)
	{
		logError("contact header already has \"*\", it could not co-exist with other URI.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pSipMsg->buf[pSipMsg->pos] == '*')
	{
		pContact->isStar = true;
		goto EXIT;
	}

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

		status = sipParserHdr_genericNameParam(pSipMsg, hdrEndPos, false, pNameParam);
		if(status != OS_STATUS_OK)
		{
			logError("contact parsing error.")
			goto EXIT;
		}
		
		osListElement_t* pLE = osList_append(&pContact->contactList, pNameParam);
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
		osList_delete(&pContact->contactList);
	}

	DEBUG_END
	return status;
}


osStatus_e sipHdrContact_encode(osMBuf_t* pSipBuf, void* pContactDT, void* pData)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrGenericNameParam_t* pContact = pContactDT;

    if(!pSipBuf || !pContact)
    {
        logError("Null pointer, pSipBuf=%p, pContact=%p.", pSipBuf, pContact);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	osMBuf_writeStr(pSipBuf, "Contact: <", true);
	sipParamUri_encode(pSipBuf, &pContact->uri);
	osMBuf_writeU8(pSipBuf, '>', true);

	osListElement_t* pParamLE = (&pContact->genericParam)->head;
	while(pParamLE)
	{
		osMBuf_writeU8(pSipBuf, ';', true);
		osPointerLen_t* pParam = pParamLE->data;
		osMBuf_writePL(pSipBuf, pParam, true);

		pParamLE = pParamLE->next;
	}

	osMBuf_writeStr(pSipBuf, "\r\n", true);

EXIT:
	return status;
}		


void sipHdrContact_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	osList_t* pContactList = &((sipHdrContact_t*)data)->contactList;
	osListElement_t* pContactLE = pContactList->head;
	while(pContactLE)
	{
		sipHdrGenericNameParam_t* pGNP = pContactLE->data;
		sipHdrGenericNameParam_cleanup(pGNP);
		pContactLE = pContactLE->next;
	}
}


void* sipHdrContact_alloc()
{
	sipHdrContact_t* pContact = osMem_alloc(sizeof(sipHdrContact_t), sipHdrContact_cleanup);
	if(!pContact)
	{
		return NULL;
	}

	osList_init(&pContact->contactList);
	pContact->isStar = false;

	return pContact;
}	
