#include "proxyMgr.h"
#include "callProxyMisc.h"


osStatus_e callProxy_addTrInfo(osListPlus_t* pList, sipRequest_e reqCode, uint32_t seqNum, void* uasId, void* uacId, bool isPrimary)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pList)
	{
		logError("null pointer, pList.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	proxyTranInfo_t* pProxyTrInfo = oszalloc(sizeof(proxyTranInfo_t), NULL);
	if(!pProxyTrInfo)
	{
		logError("fails to allocate memory for pProxyTrInfo.");
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		goto EXIT;
	}

	pProxyTrInfo->method = reqCode;
    pProxyTrInfo->seqNum = seqNum;
    pProxyTrInfo->pTransUas = uasId;
    pProxyTrInfo->pTransUac = uacId;

	status = sipProxy_addTrPair(pList, pProxyTrInfo, isPrimary);
	if(status != OS_STATUS_OK)
	{
		logError("fails to sipProxy_addTrPair (uasId=%p, uacId=%p).", uasId, uacId);
		osfree(pProxyTrInfo);
		goto EXIT;
	}

EXIT:
	return status;
}
