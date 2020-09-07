/* Copyright (c) 2019, 2020, Sean Dai
 */

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
    mDEBUG_BEGIN(LM_SIPP)

    osStatus_e status = OS_STATUS_OK;

    if(!pSipMsg || !pVia)
    {
        logError("NULL pointer, pSipMsg=%p, pVia=%p.", pSipMsg, pVia);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

	//viaList shall be initialized out of this function, otherwise, multiple via hdr will initiate the viaList each other

	pVia->viaNum = 0;
    while(pSipMsg->pos < hdrEndPos)
    {
        sipHdrViaDecoded_t* pViaElem = oszalloc(sizeof(sipHdrViaDecoded_t), sipHdrViaDecoded_cleanup);
        if(!pViaElem)
        {
            logError("could not allocate memory for pViaElem.");
            status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            goto EXIT;
        }

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
            	osfree(pViaElem);
            	status = OS_ERROR_MEMORY_ALLOC_FAILURE;
            	goto EXIT;
        	}
		}
    }

EXIT:
    if(status != OS_STATUS_OK)
    {
		osfree(pVia->pVia);
        osList_delete(&pVia->viaList);
    }

    mDEBUG_END(LM_SIPP)
    return status;
}


osStatus_e sipParserHdr_viaElement(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrVia_t* pVia)
{
    mDEBUG_BEGIN(LM_SIPP)

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

    mDEBUG_END(LM_SIPP)
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


/* via: sipHdrVia_t */
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


osStatus_e sipHdrVia_rspEncode(osMBuf_t* pSipBuf, sipHdrMultiVia_t* pTopMultiVia, sipMsgDecodedRawHdr_t* pDecodedRaw, sipHostport_t* pPeer)
{
    osStatus_e status = OS_STATUS_OK;

    if(!pSipBuf || !pDecodedRaw || !pTopMultiVia || !pPeer)
    {
        logError("null pointer, pSipBuf=%p, pDecodedRaw=%p, pTopMultiVia=%p, pPeer=%p.", pSipBuf, pDecodedRaw, pTopMultiVia, pPeer);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    sipHdrViaDecoded_t* pTopDecodedVia = pTopMultiVia->pVia;
    if(!pTopDecodedVia)
    {
        logError("pMultiVia has NULL top via.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

    sipHdrVia_t* pVia = &pTopDecodedVia->hdrValue;
    osPointerLen_t* pRport = NULL;
    osPointerLen_t rport={"rport", 5};
    if(pVia->paramNum > 1)
    {
        pRport = sipParamNV_getValuefromList(&pVia->viaParamList, &rport);
        //if there is rport, but it has been assigned a value, do nothing
        if(pRport && pRport->l != 0)
        {
            pRport = NULL;
        }
    }

    //if there is no need to manipulate rport, just copy the received received raw hdr
    if(!pRport)
    {
        sipHdrAddCtrl_t ctrl = {true, false, false, NULL};
        status = sipMsgAddHdr(pSipBuf, SIP_HDR_VIA, pDecodedRaw->msgHdrList[SIP_HDR_VIA], NULL, ctrl);
        goto EXIT;
    }

	//start to encode the top via based on the decoded hdr information
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
		//bypass rport parameter
		if(osPL_cmp(&((sipParamNameValue_t*)pViaParamLE->data)->name, &rport) == 0)
		{
			pViaParamLE = pViaParamLE->next;
			continue;
		}

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

	osMBuf_writeStr(pSipBuf, ";received=", true);
	osMBuf_writePL(pSipBuf, &pPeer->host, true);
	osMBuf_writeStr(pSipBuf, ";rport=", true);
	osMBuf_writeU32Str(pSipBuf, pPeer->portValue, true);

	//if there is other via value in the top via name
	if(pTopMultiVia->viaNum > 1)
	{
		osMBuf_writeU8(pSipBuf, ',', true);
		osList_t* pViaList = &pTopMultiVia->viaList;
		osListElement_t* pLE = pViaList->head;
		while(pLE)
		{
			sipHdrViaDecoded_t* pV = pLE->data;
			if(pV != NULL)
			{	
				sipHdr_posInfo_t* pHdrPos = &pV->hdrPos;
				osPointerLen_t viaValue = {&pDecodedRaw->sipMsgBuf.pSipMsg->buf[pHdrPos->startPos], pHdrPos->totalLen};
				osMBuf_writePL(pSipBuf, &viaValue, true);
			}

			pLE = pLE->next;
		}
	}
	else
	{				
    	osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
	}

	//copy the remaining via headers if any
	if(pDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrNum > 1)
	{
		osList_t* pRawViaList = &pDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrList;
		osListElement_t* pLE = pRawViaList->head;
		while(pLE)
		{
			osMBuf_writePL(pSipBuf, &((sipRawHdr_t*)pLE->data)->name, true);
			osMBuf_writeU8(pSipBuf, ':', true);
			osMBuf_writePL(pSipBuf, &((sipRawHdr_t*)pLE->data)->value, true);
			osMBuf_writeBuf(pSipBuf, "\r\n", 2, true);
			
			pLE = pLE->next;
		}
	}
	
EXIT:
	return status;
}


void* sipHdrMultiVia_alloc()
{
	sipHdrMultiVia_t* pVia = oszalloc(sizeof(sipHdrMultiVia_t), sipHdrMultiVia_cleanup);
	if(!pVia)
	{
		return NULL;
	}

	osList_init(&pVia->viaList);
	
	return pVia;
}


osStatus_e sipHdrVia_generateBranchId(osPointerLen_t* pBranch, char* pExtraInfo)
{
	mDEBUG_BEGIN(LM_SIPP)
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

	branchValue = osmalloc(SIP_MAX_VIA_BRANCH_ID_LEN, NULL);
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
		osfree(branchValue);
		pBranch->p = NULL;
    	pBranch->l = 0;
	}

	mDEBUG_END(LM_SIPP)
	return status;
}
		

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


//extract the peer IP and port from a via header.  If there is received, ip in the received will be used
osStatus_e sipHdrVia_getPeerTransport(sipHdrViaDecoded_t* pVia, sipHostport_t* pHostPort, sipTransport_e* pTransportProtocol)
{
	osStatus_e status = OS_STATUS_OK;
	
	if(!pVia || !pHostPort || !pTransportProtocol)
	{
		logError("null pointer, pVia=%p, pHostPort=%p, pTransportProtocol=%p.", pVia, pHostPort, pTransportProtocol);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}			

	pHostPort->portValue = pVia->hdrValue.hostport.portValue;
	if(pVia->hdrValue.paramNum > 1)
	{
		//check if there is received parameter
		osList_t* pList = &pVia->hdrValue.viaParamList;
		osListElement_t* pLE = pList->head;
		while(pLE !=NULL)
		{
			if(!osPL_strcmp(&((sipParamNameValue_t*)pLE->data)->name, "received")) 
			{
				pHostPort->host = ((sipParamNameValue_t*)pLE->data)->value;
				goto EXIT;
			}

			pLE = pLE->next;
		}
	}

	pHostPort->host = pVia->hdrValue.hostport.host;

	//for now, we just support TCP and UDP.
	if(!osPL_strcmp(&pVia->hdrValue.sentProtocol[2], "TCP"))
	{
		*pTransportProtocol = SIP_TRANSPORT_TYPE_TCP;
	}
	else
	{
		*pTransportProtocol = SIP_TRANSPORT_TYPE_UDP;
	}
EXIT:
	return status;
}


//peerViaIdx=0, 1, SIP_HDR_BOTTOM, corresponds to top, secondary, and bottom.
osStatus_e sipHdrVia_getPeerTransportFromRaw(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint8_t peerViaIdx, sipHostport_t* pHostPort, sipTransport_e* pTpProtocol)
{
	osStatus_e status = OS_STATUS_OK;
	sipHdrDecoded_t hdrDecoded = {};
	sipHdrViaDecoded_t* pHdrViaDecoded = NULL;

	switch(peerViaIdx)
	{
		case 0:
        	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &hdrDecoded, false);
			if(status != OS_STATUS_OK)
        	{
            	logError("fails to sipDecodeHdr for hdr code(%d).", SIP_HDR_VIA);
            	goto EXIT;
        	}

			pHdrViaDecoded = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->pVia;
			break;
		case 1:
		{
		    bool isMulti = sipMsg_isHdrMultiValue(SIP_HDR_VIA, pReqDecodedRaw, false, &hdrDecoded);
		    if(isMulti)
    		{
        		if(sipHdr_getHdrValueNum(&hdrDecoded) > 1)
        		{
            		//get the nexthop from the top hdr
            		pHdrViaDecoded = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->viaList.head->data;
        		}
        		else
        		{
            		//get the nexthop from the 2nd hdr
					osfree(hdrDecoded.decodedHdr);	//remove the memory allocated for the top hdr
					hdrDecoded.decodedHdr = NULL;
            		status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrList.head->data, &hdrDecoded, false);
					if(status != OS_STATUS_OK)
            		{		
                		logError("fails to sipDecodeHdr for the 2nd header entry of SIP_HDR_VIA.");
                		status = OS_ERROR_INVALID_VALUE;
                		goto EXIT;
            		}
					pHdrViaDecoded = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->pVia;
				}
			}
			else
			{
				logError("try to get the second via value, but the message does not have.");
				status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
			}
			break;
		}
		case SIP_HDR_BOTTOM:
		{
			if(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrNum == 1)
			{
            	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->pRawHdr, &hdrDecoded, false);
			}
            else if(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrNum > 1)
            {
                status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_VIA]->rawHdrList.tail->data, &hdrDecoded, false);
			}

           	if(status != OS_STATUS_OK)
           	{
               	logError("fails to sipDecodeHdr for hdr code(%d).", SIP_HDR_VIA);
               	goto EXIT;
           	}

			uint8_t viaNum = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->viaNum;
			if(viaNum == 0)
			{
           		pHdrViaDecoded = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->pVia;
			}
			else
			{
				pHdrViaDecoded = ((sipHdrMultiVia_t*)hdrDecoded.decodedHdr)->viaList.tail->data;
			}
			break;
		}
		default:
			logError("peerViaIdx(%d) is out of the range(0, 1, %d).", peerViaIdx, SIP_HDR_BOTTOM);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
			break;
	}

	if(pHdrViaDecoded)
	{
    	status = sipHdrVia_getPeerTransport(pHdrViaDecoded, pHostPort, pTpProtocol);
	}

EXIT:
	osfree(hdrDecoded.decodedHdr);
	return status;
}	
