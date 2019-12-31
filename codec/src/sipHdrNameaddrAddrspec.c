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

	status = sipParserHdr_multiNameParam(pSipMsg, hdrEndPos, false, &pAddr->addrList);
	if(status != OS_STATUS_OK)
	{
		logError("sipParserHdr_nameaddrAddrSpec error.");
		goto EXIT;
	}

	osList_t* pAddrList = &pAddr->addrList;
	osListElement_t* pAddrLE = pAddrList->head;
	while(pAddrLE)
	{
		sipHdrGenericNameParam_t* pNameParam = pAddrLE->data;
		if(!pNameParam)
		{
			logError("list for sipHdrGenericNameParam_t has null data.");
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		if(osList_getCount(&pNameParam->genericParam) !=0)
		{
			logError("error, the header has parameter(s) other than nameaddr or addrspec,"); 
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
		}

		pAddrLE = pAddrLE->next;
	}

EXIT:
	return status;
}


void sipHdrNameaddrAddrspec_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrNameaddrAddrspec_t* pAddr = data;
    osList_t* pAddrList = &pAddr->addrList;
    osListElement_t* pAddrLE = pAddrList->head;
    while(pAddrLE)
    {
        sipHdrGenericNameParam_cleanup(pAddrLE->data);

        pAddrLE = pAddrLE->next;
    }

    osList_delete(pAddrList);
}


void* sipHdrNameaddrAddrspec_alloc()
{
    sipHdrNameaddrAddrspec_t* pAddr = osMem_alloc(sizeof(sipHdrNameaddrAddrspec_t), sipHdrNameaddrAddrspec_cleanup);
    if(!pAddr)
    {
        return NULL;
    }

    osList_init(&pAddr->addrList);

    return pAddr;
}


void sipHdrNameaddrAddrspec_dealloc(void* data)
{
	osMem_deref(data);
}
