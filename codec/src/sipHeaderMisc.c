#include <string.h>

#include "osTypes.h"
#include "osMBuf.h"
#include "osMemory.h"
#include "sipHeader.h"


#if 0
//duplicate a sipHdrDecoded_t data structure.  the dst sipHdrDecoded_t will always has its own space for rawHdr
//currently this function has defect, the pointer shall point to the new raw memory, osList is not handled.  If iscomplicate to do it for all headers. replaced by function sipRawHdr_dup()
//the idea is to duplicate raw header, then decode is based on the raw header if necessary
osStatus_e sipHdrDecoded_dup(sipHdrDecoded_t* dst, sipHdrDecoded_t* src)
{
	if(! dst || ! src)
	{
		return OS_ERROR_NULL_POINTER;
	}

	*dst = *src;
	dst->isRawHdrCopied = true;

	dst->rawHdr.buf = osmalloc(src->rawHdr.size, NULL);
    memcpy(dst->rawHdr.buf, src->rawHdr.buf, src->rawHdr.size);
	osmemref(dst->decodedHdr);
	//for decodedHdr, if it is int or string, already has its own space.  if it contains osPL, need to change the osPL.p to point to new rawHdr. to-do 
	return OS_STATUS_OK;
}


//only free the spaces pointed by data structures inside pData, does not free pData data structure itself
void sipHdrDecoded_delete(sipHdrDecoded_t* pData)
{
	if(!pData)
	{
		return;
	}

	if(pData->isRawHdrCopied)
	{
		osfree(pData->rawHdr.buf);
	}

	osfree(pData->decodedHdr);
}
#endif

osStatus_e sipRawHdr_dup(sipRawHdrList_t* pRawHdrList, sipRawHdrListSA_t* pHdrSA)
{
	osStatus_e status = OS_STATUS_OK;
 
	if(!pRawHdrList || !pHdrSA)
	{
		logError("null pointer, pRawHdrList=%p, pHdrSA=%p", pRawHdrList, pHdrSA);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    //initiate pHdrSA->rawHdrList
    osList_init(&pHdrSA->rawHdr.rawHdrList);
	pHdrSA->rawHdr.pRawHdr = NULL;
	pHdrSA->pRawHdrContent = NULL;

	size_t len=0;
	if(pRawHdrList->pRawHdr)
	{
		len += pRawHdrList->pRawHdr->name.l + pRawHdrList->pRawHdr->value.l;
	}
	else
	{
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	//continue on other hdrs with the same hdr name, at the same time, create rawHdrList for hdrSA
	osListElement_t* pLE = pRawHdrList->rawHdrList.head;
	while(pLE)
	{
		len += ((sipRawHdr_t*)pLE->data)->name.l + ((sipRawHdr_t*)pLE->data)->value.l;

		sipRawHdr_t* newHdr = osmalloc(sizeof(sipRawHdr_t), NULL);
		if(!newHdr)
		{
			logError("fails to allocate memory for newHdr, size=%d", sizeof(sipRawHdr_t));
			status = OS_ERROR_MEMORY_ALLOC_FAILURE;

			goto EXIT;
		}

		osList_append(&pHdrSA->rawHdr.rawHdrList, newHdr);

		pLE = pLE->next;
	}

	pHdrSA->pRawHdrContent = osmalloc(len, NULL);
	if(!pHdrSA->pRawHdrContent)
	{
		logError("fails to allocate memory for pHdrSA->pRawHdrContent, size=%ld", len);
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		goto EXIT;
	}

	//now assign value to rawHdrList parameters
	pHdrSA->rawHdr.pRawHdr = osmalloc(sizeof(sipRawHdr_t), NULL);
	if(!pHdrSA->rawHdr.pRawHdr)
	{
		logError("fails to allocate memory for pHdrSA->rawHdrList.pRawHdr, size=%d", sizeof(sipRawHdr_t));
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;

        goto EXIT;
    }

	len = 0;
    pHdrSA->rawHdr.pRawHdr->nameCode = pRawHdrList->pRawHdr->nameCode;

    memcpy(&pHdrSA->pRawHdrContent[len], pRawHdrList->pRawHdr->name.p, pRawHdrList->pRawHdr->name.l);
	osPL_set(&pHdrSA->rawHdr.pRawHdr->name, &pHdrSA->pRawHdrContent[len], pRawHdrList->pRawHdr->name.l);
	pHdrSA->rawHdr.pRawHdr->namePos = len;
    len += pRawHdrList->pRawHdr->name.l;

	memcpy(&pHdrSA->pRawHdrContent[len], pRawHdrList->pRawHdr->value.p, pRawHdrList->pRawHdr->value.l);
	osPL_set(&pHdrSA->rawHdr.pRawHdr->value, &pHdrSA->pRawHdrContent[len], pRawHdrList->pRawHdr->value.l);
    pHdrSA->rawHdr.pRawHdr->valuePos = len;
	len += pRawHdrList->pRawHdr->value.l;

    //continue on other hdrs with the same hdr name
    pLE = pRawHdrList->rawHdrList.head;
	osListElement_t* pDupLE = pHdrSA->rawHdr.rawHdrList.head;
    while(pLE)
    {
	    ((sipRawHdr_t*)pDupLE->data)->nameCode = ((sipRawHdr_t*)pLE->data)->nameCode;
	
    	memcpy(&pHdrSA->pRawHdrContent[len], ((sipRawHdr_t*)pLE->data)->name.p, ((sipRawHdr_t*)pLE->data)->name.l);
    	osPL_set(&((sipRawHdr_t*)pDupLE->data)->name, &pHdrSA->pRawHdrContent[len], ((sipRawHdr_t*)pLE->data)->name.l);
    	((sipRawHdr_t*)pDupLE->data)->namePos = len;
    	len += ((sipRawHdr_t*)pLE->data)->name.l;

    	memcpy(&pHdrSA->pRawHdrContent[len], ((sipRawHdr_t*)pLE->data)->value.p, ((sipRawHdr_t*)pLE->data)->value.l);
        osPL_set(&((sipRawHdr_t*)pDupLE->data)->value, &pHdrSA->pRawHdrContent[len], ((sipRawHdr_t*)pLE->data)->value.l);
    	((sipRawHdr_t*)pDupLE->data)->valuePos = len;
    	len += ((sipRawHdr_t*)pLE->data)->value.l;

        pLE = pLE->next;
		pDupLE = pDupLE->next;
    }
	
EXIT:
	if(status != OS_STATUS_OK && status != OS_ERROR_NULL_POINTER)
	{
		sipRawHdrListSA_cleanup(pHdrSA);
	}

	return status;
}	


void sipRawHdrListSA_cleanup(void* pData)
{
	if(!pData)
	{
		return;
	}

	sipRawHdrListSA_t* pHdrSA = pData;

    osfree(pHdrSA->pRawHdrContent);
    osfree(pHdrSA->rawHdr.pRawHdr);
    osList_delete(&pHdrSA->rawHdr.rawHdrList);
    pHdrSA->pRawHdrContent = NULL;
	pHdrSA->rawHdr.pRawHdr = NULL;
}
