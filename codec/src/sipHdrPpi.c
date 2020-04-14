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

	status = sipParserHdr_multiNameParam(pSipMsg, hdrEndPos, false, pPpi);
	if(status != OS_STATUS_OK)
	{
		logError("sipParserHdr_multiNameParam error, status=%d.", status);
		goto EXIT;
	}

	if(pPpi->pGNP->hdrValue.genericParam.head != NULL)
	{
    	logError("PPI shall have no header parameter.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(pPpi->gnpNum > 1)
	{
		osList_t* pPpiList = &pPpi->gnpList;
		osListElement_t* pLE = pPpiList->head;
		while(pLE)
		{
			sipHdrGenericNameParamDecoded_t* pLEppi = pLE->data;
			if(pLEppi != NULL)
			{
				osList_t* pPpiParam = &pLEppi->hdrValue.genericParam;
				if(pPpiParam && pPpiParam->head != NULL)
				{
					logError("PPI shall have no header parameter.");
					osList_delete(pPpiParam);
					status = OS_ERROR_INVALID_VALUE;
					goto EXIT;
				}
			}
		}
		pLE = pLE->next;
	}

EXIT:
	if(status != OS_STATUS_OK)
	{
		osfree(pPpi);
	}

	return status;
}


