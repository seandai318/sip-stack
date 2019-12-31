#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "osDebug.h"
#include "osMisc.h"
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



osStatus_e sipParserHdr_via(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrVia_t* pVia)
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

    while(pSipMsg->pos < hdrEndPos)
    {
        sipHdrViaElement_t* pViaElem = osMem_alloc(sizeof(sipHdrViaElement_t), NULL);
        if(!pViaElem)
        {
            logError("could not allocate memory for pViaElem.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

		debug("sean, char='%c'", pSipMsg->buf[pSipMsg->pos]);
        status = sipParserHdr_viaElement(pSipMsg, hdrEndPos, pViaElem);
        if(status != OS_STATUS_OK)
        {
            logError("via parsing error.")
            goto EXIT;
        }
		
		status = osStr2Int((char*)pViaElem->hostport.port.p, pViaElem->hostport.port.l, &pViaElem->hostport.portValue);
		if(status != OS_STATUS_OK)
		{
			logError("incorrect port number (%r).", &pViaElem->hostport.port);
			goto EXIT;
		}
		logError("to remove, via host =%r, port=%d", &pViaElem->hostport.host, &pViaElem->hostport.portValue);
        osListElement_t* pLE = osList_append(&pVia->viaList, pViaElem);
        if(pLE == NULL)
        {
            logError("osList_append failure for via.");
            osMem_deref(pViaElem);
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
        osList_delete(&pVia->viaList);
    }

    DEBUG_END
    return status;
}


osStatus_e sipParserHdr_viaElement(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrViaElement_t* pVia)
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
	return status;
}



static osStatus_e sipParsing_setViaParsingInfo(sipParsing_param_e paramName, sipParsingInfo_t* pSippParsingInfo, void* arg)
{
    osStatus_e status = OS_STATUS_OK;

    sipHdrViaElement_t* pSipVia = (sipHdrViaElement_t*) arg;

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

	sipHdrViaElement_t* pVia = via;	

	osMBuf_writeStr(pSipBuf, "Via: SIP/2.0/", true);
	osMBuf_writePL(pSipBuf, &pVia->sentProtocol[2], true);
	osMBuf_writeU8(pSipBuf, ' ' , true);
   	osMBuf_writePL(pSipBuf, &pVia->hostport.host, true);
	if(pVia->hostport.portValue != 0)
	{
		osMBuf_writeU8(pSipBuf, ':', true);
    	osMBuf_writeU32Str(pSipBuf, pVia->hostport.portValue, true);
	}

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
//if pBranchId !=NULL, this function will generate a branchId, and store the generated branchId 
osStatus_e sipHdrVia_createEncode(osMBuf_t* pSipBuf, osPointerLen_t* pBranchId, char* pExtraInfo)
{
    osStatus_e status = OS_STATUS_OK;
	sipHdrViaElement_t via;

    if(!pSipBuf || !pBranchId)
    {
        logError("null pointer, pSipBuf=%p, pBranchId=%p.", pSipBuf, pBranchId);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//the default transmission method is UDP, if in transmission layer switches to TCP, can be updated there
	via.sentProtocol[2].p="UDP";
	via.sentProtocol[2].l=3;

	osList_init(&via.viaParamList);

	sipHdrVia_generateBranchId(pBranchId, pExtraInfo);

	sipConfig_getHost(&via.hostport.host, &via.hostport.portValue);

	status = sipHdrVia_encode(pSipBuf, &via, pBranchId);
	if(status != OS_STATUS_OK)
	{
		logError("via header encode fails.");
		goto EXIT;
	}

EXIT:
	return status;
}
#endif


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

	sipHdrViaElement_t* pVia = via;
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


#if 0
osStatus_e sipHdrVia_proxyBuild(sipMsgProxyRequest_t* pReq, char* viaExtraInfo)
{
    osStatus_e status = OS_STATUS_OK;

	if(!pReq)
	{
        logError("null pointer, pReq.");
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
	}
	
    sipHdrModifyInfo_t* pViaModifyInfo = osMem_alloc(sizeof(sipHdrModifyInfo_t), NULL);
    if(!pMFModifyInfo)
    {
        logError("fails to allocate memory for sipHdrModifyInfo_t.");
        status = OS_ERROR_MEMORY_ALLOC_FAILURE;
        goto EXIT;
    }

    pViaModifyInfo.modType = SIP_HDR_MODIFY_TYPE_ADD;
    sipHdrViaElement_t viaHdr = osMem;
    viaModifyInfo.pHdr = &viaHdr;
    viaModifyInfo.pData = NULL;
    viaModifyInfo.isProxy = false;
    status = sipHdrCreateModifyInfo(&viaModifyInfo, SIP_HDR_VIA, 0, false, false, &pSipRequest->viaBranchId, viaExtraInfo);
    if(status != OS_STATUS_OK)
    {
        logError("sipHdrCreateModifyInfo() fails for SIP_HDR_VIA.");
        osList_clear(&viaHdr.viaParamList);
        goto EXIT;
    }
#endif

void* sipHdrVia_alloc()
{
	sipHdrVia_t* pVia = osMem_alloc(sizeof(sipHdrVia_t), sipHdrVia_cleanup);
	if(!pVia)
	{
		return NULL;
	}

	osList_init(&pVia->viaList);
	
	return pVia;
}

	
void sipHdrVia_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipHdrVia_t* pVia = data;
	osList_t* pViaList = &pVia->viaList;
	osListElement_t* pViaLE = pViaList->head;
	while(pViaLE)
	{
		sipHdrViaElement_t* pViaElement = pViaLE->data;
		sipHostport_cleanup(&pViaElement->hostport);
		osList_delete(&pViaElement->viaParamList);
		pViaLE = pViaLE->next;
	}

	osList_delete(pViaList);
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
		

sipHdrViaElement_t* sipHdrVia_getTopBottomVia(sipHdrVia_t* pHdrViaList, bool isTop)
{
	sipHdrViaElement_t* pHdr = NULL;

	if(!pHdrViaList)
	{
		logError("null pointer, pHdrViaList.");
		goto EXIT;
	}

	osList_t* pList = &pHdrViaList->viaList;
	osListElement_t* pLE = pList->head;
	if(!isTop)
	{
		pLE = pList->tail;
	}

	if(!pLE)
	{
		goto EXIT;
	}

	pHdr = (sipHdrViaElement_t*) pLE->data;

EXIT:
	return pHdr;
}


osPointerLen_t* sipHdrVia_getTopBranchId(sipHdrViaElement_t* pHdrVia)
{
	osPointerLen_t* pl = NULL;
	if(!pHdrVia)
    {
        logError("null pointer, pHdrVia.");
        goto EXIT;
    }

	osList_t* pList = &pHdrVia->viaParamList;
	osListElement_t* pLE = pList->head;
	while(pLE)
	{
		sipParamNameValue_t* pNV = pLE->data;
		if (pNV->name.l == 6 && strncmp(pNV->name.p, "branch", 6) == 0)
		{
			pl = &pNV->value;
			break;
		}

		pLE = pLE->next;
	}

EXIT:
	return pl;
}
			
