#include "osMemory.h"
#include "osList.h"
#include "sipHdrPPrefId.h"
#include "sipGenericNameParam.h"



osStatus_e sipParserHdr_ppi(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrPpi_t* pPpi)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pSipMsg || !pPpi)
	{
		logError("NULL pointer, pSipMsg=%p, pPpi=%p.", pSipMsg, pPpi);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	status = sipParserHdr_multiNameParam(pSipMsg, hdrEndPos, false, &pPpi->ppiList);
	if(status != OS_STATUS_OK)
	{
		logError("sipParserHdr_multiNameParam error, status=%d.", status);
		goto EXIT;
	}

	osList_t* pPpiList = &pPpi->ppiList;
	osListElement_t* pLE = pPpiList->head;
	while(pLE)
	{
		sipHdrGenericNameParam_t* pLEppi = pLE->data;
		if(pLEppi != NULL)
		{
			osList_t* pPpiParam = &pLEppi->genericParam;
			if(pPpiParam && pPpiParam->head != NULL)
			{
				logError("PPI shall have no header parameter.");
				osList_delete(pPpiParam);
				status = OS_ERROR_INVALID_VALUE;
				//continue iteration until all ppi have been checked.
			}
		}
		pLE = pLE->next;
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		osList_delete(pPpiList);
	}

	return status;
}


void sipHdrPpi_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

    sipHdrPpi_t* pPpi = data;
    osList_t* pPpiList = &pPpi->ppiList;
    osListElement_t* pPpiLE = pPpiList->head;
    while(pPpiLE)
    {
        sipHdrGenericNameParam_cleanup(pPpiLE->data);

        pPpiLE = pPpiLE->next;
    }

    osList_delete(pPpiList);
}


void* sipHdrPpi_alloc()
{
	sipHdrPpi_t* pPpi = osMem_alloc(sizeof(sipHdrPpi_t), sipHdrPpi_cleanup);
	if(!pPpi)
	{
		return NULL;
	}

	osList_init(&pPpi->ppiList);	

	return pPpi;
}
