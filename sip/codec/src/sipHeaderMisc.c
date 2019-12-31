#include <string.h>

#include "osTypes.h"
#include "osMBuf.h"
#include "osMemory.h"
#include "sipHeader.h"


//duplicate a sipHdrDecoded_t data structure.  the dst sipHdrDecoded_t will always has its own space for rawHdr
osStatus_e sipHdrDecoded_dup(sipHdrDecoded_t* dst, sipHdrDecoded_t* src)
{
	if(! dst || ! src)
	{
		return OS_ERROR_NULL_POINTER;
	}

	*dst = *src;
	dst->isRawHdrCopied = true;

	dst->rawHdr.buf = osMem_alloc(src->rawHdr.size, NULL);
    memcpy(dst->rawHdr.buf, src->rawHdr.buf, src->rawHdr.size);

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
		osMem_deref(pData->rawHdr.buf);
	}

	osMem_deref(pData->decodedHdr);
}
