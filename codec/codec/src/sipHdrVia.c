#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "osDebug.h"
#include "osMisc.h"


#include "sipHdrTypes.h"
#include "sipGenericNameParam.h"
#include "sipParsing.h"
#include "osMemory.h"
#include "sipHdrVia.h"
#include "sipGenericNameParam.h"
#include "sipConfig.h"


static osStatus_e sipParsing_setViaParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg);
static sipParsingABNF_t sipViaABNF[]={  \
	{1, 1, 					SIP_TOKEN_INVALID, 	0, SIPP_PARAM_HOSTPORT, 		 sipParser_hostport, 		NULL},
    {0, SIPP_MAX_PARAM_NUM, ';', 				0, SIPP_PARAM_GENERIC_NAMEPARAM, sipParserHdr_genericParam, NULL}};
static int sipP_viaNum = sizeof(sipViaABNF)/sizeof(sipParsingABNF_t);



osStatus_e sipParserHdr_via(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrMultiVia_t* pVia)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pVia)
    {
        logError("NULL pointer, pSipMsg=%p, pVia=%p.", pSipMsg, pVia);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

//viaList shall be initialized out of this function, otherwise, multiple via hdr will initiate the viaList each other
//    osList_init(&pVia->viaList);

	pVia->viaNum = 0;
    while(pSipMsg->pos < hdrEndPos)
    {
        sipHdrViaDecoded_t* pViaElem = osMem_zalloc(sizeof(sipHdrViaDecoded_t), sipHdrViaDecoded_cleanup);
        if(!pViaElem)
        {
            logError("could not allocate memory for pViaElem.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

		debug("sean, char='%c'", pSipMsg->buf[pSipMsg->pos]);
        pViaElem->hdrPos.startPos = pSipMsg->pos;
        status = sipParserHdr_viaElement(pSipMsg, hdrEndPos, &pViaElem->hdrValue);
        if(status != OS_STATUS_OK)
        {
            logError("via parsing error.")
            goto EXIT;
        }
        pViaElem->hdrPos.totalLen = pSipMsg->pos - pViaElem->hdrPos.startPos;
		
		status = osStr2Int((char*)pViaElem->hdrValue.hostport.port.p, pViaElem->hdrValue.hostport.port.l, &pViaElem->hdrValue.hostport.portValue);
		if(status != OS_STATUS_OK)
		{
			logError("incorrect port number (%r).", &pViaElem->hdrValue.hostport.port);
			goto EXIT;
		}

		if(++pVia->viaNum == 1)
		{
			pVia->pVia = pViaElem;
		}
		else
		{
        	osListElement_t* pLE = osList_append(&pVia->viaList, pViaElem);
        	if(pLE == NULL)
        	{
            	logError("osList_append failure for via.");
            	osMem_deref(pViaElem);
            	status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            	goto EXIT;
        	}
		}
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
		osMem_deref(pVia->pVia);
        osList_delete(&pVia->viaList);
    }

    DEBUG_END
    return status;
}


osStatus_e sipParserHdr_viaElement(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrVia_t* pVia)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;
    sipParsingInfo_t sippParsingInfo[sipP_viaNum];
    SIP_INIT_PARSINGINFO(sippParsingInfo, sipP_viaNum);

    if(!pSipMsg || !pVia)
    {
        logError("NULL pointer, pSipMsg=%p, pVia=%p.", pSipMsg, pVia);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	pVia->paramNum = 0;

	// start parsing sentProtocol like SIP/2.0/TCP.  The assumption is the first char is not a LWS.
	size_t origPos = pSipMsg->pos;
	uint8_t stage = 0;
	uint8_t lwsLen = 0;
	bool isBeforeSlash = true;
	while(pSipMsg->pos < hdrEndPos && stage < 3)
	{
	//	debug("char='%c', pos=%ld, lwsLen=%d", pSipMsg->buf[pSipMsg->pos], pSipMsg->pos, lwsLen);
		if(pSipMsg->buf[pSipMsg->pos] == '/')
		{
			pVia->sentProtocol[stage].p = &pSipMsg->buf[origPos];
			pVia->sentProtocol[stage].l = pSipMsg->pos - origPos - lwsLen;
			isBeforeSlash = false;
			pSipMsg->pos++;
			stage++;
			lwsLen = 0;
			origPos = pSipMsg->pos;
			
			while(pSipMsg->pos < hdrEndPos && stage < 3)
			{
				if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
				{
					++pSipMsg->pos;
				}
				else
				{
					origPos = pSipMsg->pos;
					break;
				}
			}
		}
		else if (SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
		{
			if(stage == 2)
			{
	            pVia->sentProtocol[stage].p = &pSipMsg->buf[origPos];
	            pVia->sentProtocol[stage].l = pSipMsg->pos - origPos;
				pSipMsg->pos++;
				origPos = pSipMsg->pos;
				while(pSipMsg->pos < hdrEndPos)
				{
					if(SIP_IS_LWS(pSipMsg->buf[pSipMsg->pos]))
					{
						++pSipMsg->pos;
	                }
    	            else
        	        {
						origPos = pSipMsg->pos;
            	        break;
                	}
				}
				break;
            }
			else
			{
				lwsLen++;
				pSipMsg->pos++;
			}
        }
		else
		{
			pSipMsg->pos++;
		}
	}

	sipParsingInfo_t parentParsingInfo;
	parentParsingInfo.arg = pVia;
    parentParsingInfo.tokenNum = 1;
    parentParsingInfo.token[0]=',';
	sipParsingStatus_t parsingStatus;
    sipParsing_setParsingInfo(sipViaABNF, sipP_viaNum, &parentParsingInfo, sippParsingInfo, sipParsing_setViaParsingInfo);

    status = sipParsing_getHdrValue(pSipMsg, hdrEndPos, sipViaABNF, sippParsingInfo, sipP_viaNum, &parsingStatus);

EXIT:
	DEBUG_END

	if(status == OS_STATUS_OK)
	{
		pVia->paramNum = 0;
		osPointerLen_t branchName = {"branch", 6};
		pVia->pBranch = sipParamNV_takeNVfromList(&pVia->viaParamList, &branchName);
		if(!pVia->pBranch)
		{
			logError("the via does not contain branch.");
			status = OS_ERROR_INVALID_VALUE;
		}
		else
		{
			pVia->paramNum = osList_getCount(&pVia->viaParamList) + 1;
		}
	}

	return status;
}



static osStatus_e sipParsing_setViaParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    sipHdrVia_t* pSipVia = (sipHdrVia_t*) arg;

    switch (paramName)
    {
        case SIPP_PARAM_HOSTPORT:
            pSippParsingInfo->arg = &pSipVia->hostport;
            break;

        case SIPP_PARAM_GENERIC_NAMEPARAM:
            osList_init(&pSipVia->viaParamList);
            pSippParsingInfo->arg = &pSipVia->viaParamList;
            break;

        default:
            logError("unexpected parameter for Via parameter, sipViaABNF.paramName=%s.", paramName);
            status = OS_ERROR_INVALID_VALUE;
    }

    return status;
}


osStatus_e sipHdrVia_encode(osMBuf_t* pSipBuf, void* via, void* data)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pSipBuf || !via)
	{
		logError("null pointer, pSipBuf=%p, via=%p.", pSipBuf, via);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	sipHdrVia_t* pVia = via;	
	if(pVia->pBranch == NULL || pVia->pBranch->value.l == 0)
	{
		logError("pBranch is NULL(%p) or branch value is empty.", pVia->pBranch);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	osMBuf_writeStr(pSipBuf, "Via: SIP/2.0/", true);
	osMBuf_writePL(pSipBuf, &pVia->sentProtocol[2], true);
	osMBuf_writeU8(pSipBuf, ' ' , true);
   	osMBuf_writePL(pSipBuf, &pVia->hostport.host, true);
	if(pVia->hostport.portValue != 0)
	{
		osMBuf_writeU8(pSipBuf, ':', true);
    	osMBuf_writeU32Str(pSipBuf, pVia->hostport.portValue, true);
	}

	//write branch params
    osMBuf_writeStr(pSipBuf, ";branch=", true);
    osMBuf_writePL(pSipBuf, &pVia->pBranch->value, true);

    //write other via params
   	osList_t* pViaParamList = &pVia->viaParamList;
   	osListElement_t* pViaParamLE = pViaParamList->head;
   	while(pViaParamLE)
   	{
    	osMBuf_writeU8(pSipBuf, ';', true);
       	sipHdrParamNameValue_t* pParam = pViaParamLE->data;
		osMBuf_writePL(pSipBuf, &pParam->name, true);
		if(pParam->value.l)
		{
			osMBuf_writeU8(pSipBuf, '=', true);
       		osMBuf_writePL(pSipBuf, &pParam->value, true);
		}
       	pViaParamLE = pViaParamLE->next;
   	}

 	osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);

EXIT:
	return status;
}

#if 0
osStatus_e sipHdrVia_create(void* via, void* branchId, void* other)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrParamNameValue_t* pBranchParam = NULL;

    if(!via || !branchId)
    {
        logError("null pointer, via=%p, branchId=%p.", via, branchId);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	sipHdrVia_t* pVia = via;
	osPointerLen_t* pBranchId = branchId;
	char* pExtraInfo = other;

    //the default transmission method is UDP, if in transmission layer switches to TCP, can be updated there
    pVia->sentProtocol[2].p="UDP";
    pVia->sentProtocol[2].l=3;

	sipHdrVia_generateBranchId(pBranchId, pExtraInfo);
	sipConfig_getHost(&pVia->hostport.host, &pVia->hostport.portValue);
		
	pBranchParam = osMem_alloc(sizeof(sipHdrParamNameValue_t), NULL);
	if(!pBranchParam)
	{
		logError("pBranchParam allocation fails.");
		status = OS_ERROR_MEMORY_ALLOC_FAILURE;
		goto EXIT;
	}

	pBranchParam->name.p = "Branch";
	pBranchParam->name.l = 6;
	pBranchParam->value.p = pBranchId->p;
	pBranchParam->value.l = pBranchId->l;

    osList_init(&pVia->viaParamList);
	osList_append(&pVia->viaParamList, pBranchParam);

EXIT:
	if(status != OS_STATUS_OK)
	{
		osMem_deref(pBranchParam);
	}

	return status;
}
#endif


void* sipHdrMultiVia_alloc()
{
	sipHdrMultiVia_t* pVia = osMem_zalloc(sizeof(sipHdrMultiVia_t), sipHdrMultiVia_cleanup);
	if(!pVia)
	{
		return NULL;
	}

	osList_init(&pVia->viaList);
	
	return pVia;
}


osStatus_e sipHdrVia_generateBranchId(osPointerLen_t* pBranch, char* pExtraInfo)
{
	DEBUG_BEGIN
	osStatus_e status = OS_STATUS_OK;
	char* branchValue = NULL;

	if(!pBranch)
	{
		logError("null pointer, pBranch.");
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	pBranch->p = NULL;
	pBranch->l = 0;

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp); 
	srand(tp.tv_nsec);
	int randValue=rand();

	branchValue = osMem_alloc(SIP_MAX_VIA_BRANCH_ID_LEN, NULL);
    if(branchValue == NULL)
    {
        logError("allocate branchId fails.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	if(pExtraInfo)
	{
		pBranch->l = sprintf(branchValue, "z9hG4bK%s:%lx%s%x", pExtraInfo, tp.tv_nsec % tp.tv_sec, osGetNodeId(), randValue);
	}
	else
	{
		pBranch->l = sprintf(branchValue, "z9hG4bK%lx%s%x", tp.tv_nsec % tp.tv_sec, osGetNodeId(), randValue);
	}

	if(pBranch->l >= SIP_MAX_VIA_BRANCH_ID_LEN)
	{
		pBranch->l = 0;
		logError("the branchId length (%ld) exceeds the maximum allowable length.", pBranch->l);
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pBranch->p = branchValue;

EXIT:
	if(status != OS_STATUS_OK)
	{
		osMem_deref(branchValue);
		pBranch->p = NULL;
    	pBranch->l = 0;
	}

	DEBUG_END
	return status;
}
		
#if 0
sipHdrVia_t* sipHdrVia_getTopBottomVia(sipHdrMultiVia_t* pHdrViaList, bool isTop)
{
	sipHdrVia_t* pHdr = NULL;

	if(!pHdrViaList)
	{
		logError("null pointer, pHdrViaList.");
		goto EXIT;
	}

	if(isTop || pHdrViaList->viaList.tail == NULL)
	{
		if(pHdrViaList->isDecoded)
		{
			pHdr = pHdrViaList->u.pDecodedVia ? &pHdrViaList->u.pDecodedVia->hdrValue : NULL;
		}
		else
		{
			pHdr = pHdrViaList->u.pVia;
		}
		goto EXIT;
	}

    if(pHdrViaList->isDecoded)
    {
		pHdr = &((sipHdrViaDecoded_t*)pHdrViaList->viaList.tail->data)->hdrValue;
	}
	else
	{
		pHdr = pHdrViaList->viaList.tail->data;
	}

EXIT:
	return pHdr;
}
#endif

osPointerLen_t* sipHdrVia_getTopBranchId(sipHdrMultiVia_t* pHdrVia)
{
	osPointerLen_t* pl = NULL;
	if(!pHdrVia)
    {
        logError("null pointer, pHdrVia.");
        goto EXIT;
    }

	sipParamNameValue_t* pBranch;
	pBranch = pHdrVia->pVia ? pHdrVia->pVia->hdrValue.pBranch : NULL;

	if(pBranch)
	{
		pl = &pBranch->value;
	}

EXIT:
	return pl;
}
			
