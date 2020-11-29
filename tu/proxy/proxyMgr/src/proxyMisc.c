/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file proxyMisc.c
 ********************************************************/

#include "osDebug.h"
#include "osTypes.h"
#include "osList.h"

#include "proxyMgr.h"


osStatus_e sipProxy_addTrPair(osListPlus_t* pList, proxyTranInfo_t* pProxyTrInfo, bool isPrimary)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pList || !pProxyTrInfo)
	{
		logError("null pointer, pList=%p, pProxyTrInfo=%p,", pList, pProxyTrInfo);
		status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pList->num > 0)
	{
		logError("add primary proxyTrInfo, but there is already proxyTrInfo existed.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	if(isPrimary)
	{
		pList->first = pProxyTrInfo;
		pList->num = 1;
	}
	else
	{
		osList_append(&pList->more, pProxyTrInfo);
		pList->num += 1;
	}

EXIT:
	return status;
}


//when this function is called, it assumes uasTrId has already been stored earlier
osStatus_e sipProxy_updatePairUacTrId(osListPlus_t* pList, void* uasTrId, void* uacTrId, bool isPrimary)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pList || !uasTrId || !uacTrId)
	{
		logError("null pointer, pList=%p, uasTrId=%p, uacTrId=%p", pList, uasTrId, uacTrId);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	if(isPrimary)
	{
		if(((proxyTranInfo_t*)pList->first)->pTransUas == uasTrId)
		{
			((proxyTranInfo_t*)pList->first)->pTransUac = uacTrId;
		}
		goto EXIT;
	}
	else
	{
        osListElement_t* pLE = pList->more.head;
        while (pLE)
        {
            if(((proxyTranInfo_t*)pLE->data)->pTransUas == uasTrId)
            {
                ((proxyTranInfo_t*)pList->first)->pTransUac = uacTrId;
				goto EXIT;
                break;
            }

            pLE = pLE->next;
        }
	}

	status = OS_ERROR_INVALID_VALUE;

EXIT:
	return status;
}

	
//if isPrimary=1, get from pList->first.  if isRemove=1, other than get uasTrId, also remove the associated pair
void* sipProxy_getPairUasTrId(osListPlus_t* pList, void* uacTrId, bool isPrimary, bool isRemove)
{
	void* pUasTrId = NULL;
	if(!pList)
	{
		logError("null pointer, pList.");
		goto EXIT;
	}

	if(isPrimary)
	{
	    if(!pList->first)
    	{
        	logError("pList->first is null.");
        	goto EXIT;
    	}

		if(((proxyTranInfo_t*)pList->first)->pTransUac == uacTrId)
		{
			pUasTrId = ((proxyTranInfo_t*)pList->first)->pTransUas;
			if(isRemove)
			{
				osfree(pList->first);
				pList->first = NULL;
			}
		}
	}
	else
	{
		osListElement_t* pLE = pList->more.head;
		while (pLE)
		{
			if(((proxyTranInfo_t*)pLE->data)->pTransUac == uacTrId)
			{
				pUasTrId = ((proxyTranInfo_t*)pLE->data)->pTransUas;
				break;
			}
		
			pLE = pLE->next;
		}

		if(isRemove && pLE != NULL)
		{
			osList_deleteElementAll(pLE, true);
		}
	}

EXIT:
	return pUasTrId;
}


void sipProxy_getPairPrimaryTrId(osListPlus_t* pList, void** ppUasId, void** ppUacId)
{
    if(!pList)
    {
        logError("null pointer, pList.");
        return;
    }

	*ppUasId = ((proxyTranInfo_t*)pList->first)->pTransUas;
	*ppUacId = ((proxyTranInfo_t*)pList->first)->pTransUac;
}


void* sipProxy_getPairPrimaryUasTrId(osListPlus_t* pList)
{
    void* pUasTrId = NULL;
    if(!pList)
    {
        logError("null pointer, pList.");
        goto EXIT;
    }

    if(!pList->first)
    {
		logError("pList->first is null.");
		goto EXIT;
	}

	pUasTrId = ((proxyTranInfo_t*)pList->first)->pTransUas;

EXIT:
	return pUasTrId;
}


osStatus_e sipProxy_removePairTrInfo(osListPlus_t* pList, void* trId, bool isPrimary)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pList)
	{
		logError("null pointer, pList.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

    if(pList->num == 0)
    {
        logError("try to remove a transaction pair, but the number of transaction pair=0.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	if(isPrimary)
	{
		if(pList->first)
		{
			osfree(pList->first);
			pList->first = NULL;
			pList->num--;
		}
	}
	else
	{
        osListElement_t* pLE = pList->more.head;
        while (pLE)
        {
            if(((proxyTranInfo_t*)pLE->data)->pTransUac == trId || ((proxyTranInfo_t*)pLE->data)->pTransUas == trId)
            {
                break;
            }

            pLE = pLE->next;
        }

		if(pLE)
		{
			osList_deleteElementAll(pLE, true);
			pList->num--;
		}
		else
		{
			logError("try to remove a secondary transaction pair, but it does not exist (trId=0x%x).", trId); 	
            status = OS_ERROR_INVALID_VALUE;
		}
    }
	
EXIT:
	return status;
}
