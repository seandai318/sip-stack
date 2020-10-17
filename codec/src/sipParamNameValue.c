/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipParamNameValue.c
 ********************************************************/

#include "osTypes.h"
#include "osMemory.h"

#include "sipParamNameValue.h"
#include "osDebug.h"


bool sipParamNV_isNameExist(osList_t* pParam, osPointerLen_t* pName)
{
	bool isExist = false;

    if(!pParam || !pName)
    {
        logError("null pointer, pParam=%p, pName=%p.", pParam, pName);
        goto EXIT;
    }

    osListElement_t* pLE = ((osList_t*)pParam)->head;
    while(pLE)
    {
        if(osPL_cmp(&((sipParamNameValue_t*)pLE->data)->name, pName) == 0)
		{
			isExist = true;
			break;
		}

		pLE = pLE->next;
	}

EXIT:
	return isExist;
}


osPointerLen_t* sipParamNV_getValuefromList(osList_t* pParam, osPointerLen_t* pName)
{
	osPointerLen_t* pValue = NULL;

	if(!pParam || !pName)
	{
		logError("null pointer, pParam=%p, pName=%p.", pParam, pName);
		goto EXIT;
	}

	osListElement_t* pLE = ((osList_t*)pParam)->head;
	while(pLE)
	{
		if(osPL_cmp(&((sipParamNameValue_t*)pLE->data)->name, pName) == 0)
		{
			pValue = &((sipParamNameValue_t*)pLE->data)->value;
			break;
		}

		pLE = pLE->next;
	}

EXIT:
	return pValue;
}


//remove the top NV element from a list, and return the removed NV.
sipParamNameValue_t* sipParamNV_takeTopNVfromList(osList_t* pParamList, uint8_t* newListCount)
{
    sipParamNameValue_t* pNV = NULL;

    if(!pParamList)
    {
        logError("null pointer, pParam=%p.", pParamList);
        goto EXIT;
    }

	if(newListCount)
	{
		*newListCount = 0;
	}

	uint8_t lCount = osList_getCount(pParamList);
	if(lCount >= 1)
	{
    	osListElement_t* pLE = pParamList->head;
		pNV = pLE->data;
		pParamList->head = pLE->next;
		if(lCount == 1)
		{
			pParamList->tail = NULL;
		}

		if(newListCount)
		{
			*newListCount = --lCount;
		}
		osfree(pLE);
	}
		
EXIT:
	return pNV;
}	


//remove a NV element from a list, and return the removed NV.
sipParamNameValue_t* sipParamNV_takeNVfromList(osList_t* pParam, osPointerLen_t* pName)
{
    sipParamNameValue_t* pNV = NULL;

    if(!pParam || !pName)
    {
        logError("null pointer, pParam=%p, pName=%p.", pParam, pName);
        goto EXIT;
    }

logError("to-remove, branch free, pParam=%p, count=%d", pParam, osList_getCount(pParam));
    osListElement_t* pLE = pParam->head;
    while(pLE)
    {
        if(osPL_cmp(&((sipParamNameValue_t*)pLE->data)->name, pName) == 0)
		{
			pNV = pLE->data;
			if(pLE->prev == NULL && pLE->next == NULL)
			{
				pParam->head = NULL;
                pParam->tail = NULL;
			}
			else if(pLE->prev == NULL)
			{
				pParam->head = pLE->next;
				pLE->next->prev = NULL;
			}
			else if(pLE->next == NULL)
			{
				pParam->tail = pLE->prev;
				pLE->prev->next = NULL;
			}
			else
			{
				pLE->prev->next = pLE->next;
				pLE->next->prev = pLE->prev;
			}

logError("to-remove, branch free, pLE=%p", pLE);
			osfree(pLE);
			break;
		}

		pLE = pLE->next;
	}

EXIT:
logError("to-remove, after branch free, pParam=%p, count=%d", pParam, osList_getCount(pParam));
	return pNV;
}
