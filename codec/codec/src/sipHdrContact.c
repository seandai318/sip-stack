
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




osStatus_e sipParserHdr_contact(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiContact_t* pContact)
{
	DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pContact)
    {
        logError("NULL pointer, pSipMsg=%p, pContact=%p.", pSipMsg, pContact);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pSipMsg->buf[pSipMsg->pos] == '*')
	{
		pContact->isStar = true;
		goto EXIT;
	}

	pContact->contactList.gnpNum = 0;

	status = sipParserHdr_multiNameParam(pSipMsg, hdrEndPos, false, &pContact->contactList);
	if(status != OS_STATUS_OK)
	{
		logError("parse sipParserHdr_multiNameParam for contact fails.");
		goto EXIT;
	}

EXIT:
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

#if 0
void sipHdrMultiContact_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdrMultiContact_t* pContactHdr = data;
	sipHdrMultiGenericNameParam_cleanup(&pContactHdr->contactList);
}
#endif

void* sipHdrMultiContact_alloc()
{
	sipHdrMultiContact_t* pContact = osMem_zalloc(sizeof(sipHdrMultiContact_t), sipHdrMultiContact_cleanup);
	if(!pContact)
	{
		return NULL;
	}

//	osList_init(&pContact->contactList);
	pContact->isStar = false;

	return pContact;
}	
