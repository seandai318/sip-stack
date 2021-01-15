/******************************************************************************
 * Copyright (C) 2019, 2020, Sean Dai
 *
 * @file osTuDestMgr.c
 * manage the remote addresses gotten from dns query by app.
 * App queries dns and pass in the dns response.  this function parses the dns response, sort all A record (IP address)
 * based on the priority and weight, so that it can return the best next  hop to the app.
 * this function also implements quarantine to block a destination IP not be used for certain time.  Even though usually everytime
 * an app requests for best next hop, it will pass in a dns response.  If the dns response was from the cached records in the dns 
 * module, this function will not parse the dns response, instead, just return the next best address based on the stored records.
 *  The stored records maintains a keep alive timer.  When keepAlive time expires, or a new dns response that was fresh fetched 
 * from a dns server (i.e., not from th cached records), this function will destroy the old rcords, and create new one, and start
 * new keep alive timer.
 ******************************************************************************/

#include <string.h>
#include <arpa/inet.h>

#include "osDebug.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "sipTU.h"
#include "sipTuDestMgr.h"



static osStatus_e sipTuDest_msgAddRec(dnsMessage_t* pRR, sipTuDestRecord_t* pDestRec);
static osStatus_e sipTUDest_msgListAddARec(sipTuDestRecord_t* pDestRec, dnsMessage_t* pDnsRsp, dnsResResponse_t* pRR);
static dnsMessage_t* sipTuDest_getRRInfo(dnsResResponse_t* pRR, osPointerLen_t* qName, bool* isNoError);
static osListElement_t* sipTuDest_getDestRec(osPointerLen_t* qName);
static sipTuDestInfo_t* sipTuDest_isDestExist(struct in_addr ipAddr, int port, int priority, int weight, osList_t* pDestList);
static int sipTuDest_msgGetSrvInfo(osList_t* pRRAddtlAnswerList, char* aQName, sipTuDestSrvInfo_t srvInfo[]);
static int sipTuDest_msgListGetSrvInfo(dnsResResponse_t* pRR, char* aQName, sipTuDestSrvInfo_t srvInfo[]);
static bool sipTuDest_sortDestInfo(osListElement_t *le1, osListElement_t *le2, void *arg);
static bool sipTuDest_getNextHop(sipTuDestRecord_t* pDestRec, sipTuAddr_t* pNextHop);
static int sipTuDest_getSamePriorityNum(osListElement_t* pLE, int* weightSum);
static void sipTuDest_onQTimeout(uint64_t timerId, void* data);
static void sipTuDest_onKATimeout(uint64_t timerId, void* data);
static void sipTuDest_dbgList();
static void sipTuDestRecord_cleanup(void* data);
static void sipTuDestInfo_cleanup(void* data);


static __thread osList_t gDestRecordList;				//contains sipTuDestRecord_t


//if isDnsResCached = true, the dnsResponse was cached in the dns module, implies there is no change to the pDnsResponse.
/* algorithm:
 * 1. if dnsResponse contains only one dsnMessage_t, 
 * 1.1 A records are in the additionalAnswerList when qury is for NAPTR and SRV, and A records are in answerList when query is for A
 * 1.2 first find a A record, then based on the A record's qName to find the matching SRV record (A Qname = SRV target name) to find priority and weight
 * 2. if dnsResponse contains multiple dnsMessages
 * 2.1 first find the highest query type to identify the qName, which will be used to fill the sipTuDestRecord_t.
 * 2.2 then find a A record. and go through the all dnsMessage_t in the dnsResponse to find the corresponding SRV for priority/weight/port
 * 2.3 if A record is found in additionalAnswer section, then the SRV shall be in the same dnsMessage_t
 * all A records are pushed into the destList in the order of priority and weight, smaller value of priority and the same priority but smaller weight are in the front
 */
bool sipTu_getBestNextHop(dnsResResponse_t* pRR, bool isDnsResCached, sipTuAddr_t* pNextHop)
{
	bool isFound = false;
	osStatus_e status = OS_STATUS_OK;

	if(!pRR || !pNextHop)
	{
		logError("null pointer, pRR=%p, pNextHop=%p.", pRR, pNextHop);
		goto EXIT;
	}

	bool isNoError = false;
	osPointerLen_t qName;
	dnsMessage_t* pDnsRsp = sipTuDest_getRRInfo(pRR, &qName, &isNoError);
	if(!pDnsRsp)
	{
		logError("fails to sipTuDest_getRRInfo.");
		goto EXIT;
	}

	mdebug(LM_SIPAPP, "pRR->rrType=%d, qName=%r, isDnsResCached=%d, isNoError=%d", pRR->rrType, &qName, isDnsResCached, isNoError);
sipTuDest_dbgList();

	bool isAddDestRec = false;
	sipTuDestRecord_t* pDestRec = NULL;
	switch(pRR->rrType)
	{
		case DNS_RR_DATA_TYPE_MSG:
			//isDnsResCached=true means the same dns record already exists in the dns module, implies the sipTuDest shall already have the record  
			if(isDnsResCached)
			{
				osListElement_t* pLE = sipTuDest_getDestRec(&qName);
				if(pLE)
				{
					//in the reality, when isDnsResCached == true, isNoError shall always to be true
					if(isNoError)
					{
						pDestRec = pLE->data;
						break;
					}
					else
					{
						osList_deleteElementAll(pLE, true);
					}

					break;
				}
			}

            //if !isDnsResCached or pLE ==NULL (may happen when the dest KA times out, but on dns cache side, there is still record)				
			if(isNoError)
			{
				pDestRec = oszalloc(sizeof(sipTuDestRecord_t), sipTuDestRecord_cleanup);
				status = sipTuDest_msgAddRec(pDnsRsp, pDestRec);
				isAddDestRec = true;
			}
			break;			
		case DNS_RR_DATA_TYPE_MSGLIST:
			//isDnsResCached=true means the same dns record already exists in the dns module, implies the sipTuDest shall already have the record
            if(isDnsResCached)
            {
            	osListElement_t* pLE = sipTuDest_getDestRec(&qName);
            	if(pLE)
            	{
                    //in the reality, when isDnsResCached == true, isNoError shall always to be true
                	if(isNoError)
                	{
                    	pDestRec = pLE->data;
                    	break;
                	}
                	else
                	{
                    	osList_deleteElementAll(pLE, true);
                	}

					break;
            	}
			}
		
			//if !isDnsResCached or pLE ==NULL (may happen when the dest KA times out, but on dns cache side, there is still record)
			if(isNoError)
			{
           		pDestRec = oszalloc(sizeof(sipTuDestRecord_t), sipTuDestRecord_cleanup);
				status = sipTuDest_msgAddRec(pDnsRsp, pDestRec);

				osListElement_t* pRRLE = pRR->dnsRspList.head;
				while(pRRLE && pRRLE->data != pDnsRsp)
				{
					sipTUDest_msgListAddARec(pDestRec, pRRLE->data, pRR);

					pRRLE = pRRLE->next;
				}

               	isAddDestRec = true;
			}
			break;
		case DNS_RR_DATA_TYPE_STATUS:
		default:
			logError("rrType(%d) is not expected.", pRR->rrType);
			goto EXIT;
			break;
	}

	if(isAddDestRec)
	{
		osList_append(&gDestRecordList, pDestRec);
		pDestRec->kaTimerId = osStartTimer(SIP_TU_DEST_KEEP_ALIVE_TIME, sipTuDest_onKATimeout, pDestRec);
	}

	if(isNoError && pDestRec)
	{
		isFound = sipTuDest_getNextHop(pDestRec, pNextHop);
	}

    mdebug(LM_SIPAPP, "qName=%r, isFound=%d%s%A", &qName, isFound, isFound ? ", pNextHop->sockAddr=" : "", isFound ? &pNextHop->sockAddr: NULL);

EXIT:
	return isFound;
}


bool sipTu_getBestNextHopByName(osPointerLen_t* pQName, sipTuAddr_t* pNextHop)
{
    bool isFound = false;
    osListElement_t* pLE = gDestRecordList.head;
    while(pLE)
    {
        sipTuDestRecord_t* pDestRec = pLE->data;
        if(osPL_cmp(&pDestRec->destName.pl, pQName) == 0)
        {
			isFound = sipTuDest_getNextHop(pDestRec, pNextHop);
			break;
		}

		pLE = pLE->next;
	}

	return isFound;
}

//app notifies a dest is not accessable, quarantine this dest
bool sipTu_setDestFailure(osPointerLen_t* dest, struct sockaddr_in* destAddr)
{
	bool isFound = false;
	osListElement_t* pLE = gDestRecordList.head;
	while(pLE)
	{
		sipTuDestRecord_t* pDestRec = pLE->data;
		if(osPL_cmp(&pDestRec->destName.pl, dest) == 0)
		{
			osListElement_t* pAddrLE = pDestRec->destList.head;
			while(pAddrLE)
			{
				sipTuDestInfo_t* pDestInfo = pAddrLE->data;
				if(osIsSameSA(&pDestInfo->destAddr, destAddr))
				{
					if(!pDestInfo->isQuarantined)
					{
						pDestInfo->isQuarantined = true;
						pDestInfo->qTimerId = osStartTimer(SIP_TU_DEST_QUARANTINE_TIME, sipTuDest_onQTimeout, pDestInfo);
						isFound = true;
					}
					break;
				}
			
				pAddrLE = pAddrLE->next;
			}

			break;
		}

		pLE = pLE->next;
	}

	return isFound;
}


//pNextHop INOUT, input as a failed destination, output as a new nextHop
bool sipTu_replaceDest(osPointerLen_t* dest, sipTuAddr_t* pNextHop)
{
    bool isFound = false;

	if(!pNextHop->isSockAddr)
	{
		logError("expect pNextHop->isSockAddr = true, but instead, it passed in as false.");
		goto EXIT;
	}

    osListElement_t* pLE = gDestRecordList.head;
    while(pLE)
    {
        sipTuDestRecord_t* pDestRec = pLE->data;
        if(osPL_cmp(&pDestRec->destName.pl, dest) == 0)
        {
            osListElement_t* pAddrLE = pDestRec->destList.head;
            while(pAddrLE)
            {
                sipTuDestInfo_t* pDestInfo = pAddrLE->data;
                if(osIsSameSA(&pDestInfo->destAddr, &pNextHop->sockAddr))
                {
                    if(!pDestInfo->isQuarantined)
                    {
                        pDestInfo->isQuarantined = true;
                        pDestInfo->qTimerId = osStartTimer(SIP_TU_DEST_QUARANTINE_TIME, sipTuDest_onQTimeout, pDestInfo);
                        isFound = true;
                    }
                    break;
                }

                pAddrLE = pAddrLE->next;
            }

            isFound = sipTuDest_getNextHop(pDestRec, pNextHop);
            break;
        }

        pLE = pLE->next;
    }

EXIT:
    return isFound;
}


//for local set destination (not from dns query), the kaTimerId would not be set, i.e., never expires
void sipTuDest_localSet(osPointerLen_t* dest, struct sockaddr_in destAddr, transportType_e tpType)
{
	if(!dest)
	{
		logError("null pointer, pDestName=%p.", dest);
		return;
	}

	sipTuDestRecord_t* pDestRec = NULL;
    osListElement_t* pLE = gDestRecordList.head;
    while(pLE)
    {
        pDestRec = pLE->data;
        if(osPL_cmp(&pDestRec->destName.pl, dest) == 0)
        {
			break;
		}

		pLE = pLE->next;
	}

	sipTuDestInfo_t* pDestInfo = osmalloc(sizeof(sipTuDestInfo_t), NULL);
	pDestInfo->tpType = tpType;
	pDestInfo->destAddr = destAddr;
	pDestInfo->isQuarantined = false;
	pDestInfo->qTimerId = 0;

    if(!pDestRec)
	{
		pDestRec = oszalloc(sizeof(sipTuDestRecord_t), sipTuDestRecord_cleanup);
		osVPL_copyPL(&pDestRec->destName, dest); 
		osList_append(&gDestRecordList, pDestRec);
	}

    osList_append(&pDestRec->destList, pDestInfo);
}


bool sipTuDest_isQuarantined(osPointerLen_t* dest, struct sockaddr_in destAddr)
{
	osListElement_t* pLE = gDestRecordList.head;
	while(pLE)
	{
		sipTuDestRecord_t* pDestRec = pLE->data;
        if(osPL_cmp(&pDestRec->destName.pl, dest) == 0)
		{
            osListElement_t* pAddrLE = pDestRec->destList.head;
            while(pAddrLE)
            {
                sipTuDestInfo_t* pDestInfo = pAddrLE->data;
                if(osIsSameSA(&pDestInfo->destAddr, &destAddr))
                {
                    return pDestInfo->isQuarantined;
				}

				pAddrLE = pAddrLE->next;
			}
		}

		pLE = pLE->next;
	}

	//if do not find a match, assume is quarantined
	logError("gDestRecordList does not contain local address(%A).", &destAddr);
	return true;
}
	

static void sipTuDest_onQTimeout(uint64_t timerId, void* data)
{
	if(!data)
	{
		logError("null pointer, data.");
		return;
	}

	sipTuDestInfo_t* pDestInfo = data;
	if(pDestInfo->qTimerId != timerId || !pDestInfo->isQuarantined)
	{
		logError("timerId does not match(pDestInfo->qTimerId=0x%lx, timerId=0x%ls), or the destInfo is not quarantined(%d).", pDestInfo->qTimerId, timerId, pDestInfo->isQuarantined);
		return;
	}

	pDestInfo->isQuarantined = false;
	pDestInfo->qTimerId = 0;
}


static void sipTuDest_onKATimeout(uint64_t timerId, void* data)
{
    if(!data)
    {
        logError("null pointer, data.");
        return;
    }

	sipTuDestRecord_t* pDestRec = data;
	if(pDestRec->kaTimerId != timerId)
	{
		logError("timerId does not match(pDestInfo->kaTimerId=0x%lx, timerId=0x%ld).", pDestRec->kaTimerId, timerId);
		return;
	}
	pDestRec->kaTimerId = 0;

	mdebug(LM_SIPAPP, "ka timerId(0x%lx) times out, delete pDestRec(%p) for destName=%r from gDestRecordList.", timerId, pDestRec, &pDestRec->destName);
	osList_deletePtrElement(&gDestRecordList, pDestRec);
	osfree(pDestRec);
}


//create pDestRec based on pDnsRsp
static osStatus_e sipTuDest_msgAddRec(dnsMessage_t* pDnsRsp, sipTuDestRecord_t* pDestRec)
{
	osStatus_e status = OS_STATUS_OK;
	
	switch(pDnsRsp->query.qType)
	{
		case DNS_QTYPE_A:
		{
			osPointerLen_t qName ={pDnsRsp->query.qName, strlen(pDnsRsp->query.qName)};
			osVPL_copyPL(&pDestRec->destName, &qName);	

            osListElement_t* pLE = pDnsRsp->answerList.head;
            while(pLE)
            {
				sipTuDestInfo_t* pDestInfo = osmalloc(sizeof(sipTuDestInfo_t), sipTuDestInfo_cleanup);

				pDestInfo->tpType = TRANSPORT_TYPE_ANY;
				pDestInfo->priority = 0;
				pDestInfo->weight = 0;
                pDestInfo->destAddr.sin_addr = ((dnsRR_t*)pLE->data)->ipAddr;
                pDestInfo->destAddr.sin_port=0;
                pDestInfo->destAddr.sin_family = AF_INET;

				osList_orderAppend(&pDestRec->destList, sipTuDest_sortDestInfo, pDestInfo, NULL);

                pLE = pLE->next;
            }
            break;
		}
		case DNS_QTYPE_SRV:
		{
            osPointerLen_t qName ={pDnsRsp->query.qName, strlen(pDnsRsp->query.qName)};
            osVPL_copyPL(&pDestRec->destName, &qName);

			transportType_e tpType = TRANSPORT_TYPE_ANY;
            if(osPL_findStr(&qName, "_sip._udp.", 0))
            {
                tpType = TRANSPORT_TYPE_UDP;
            }
            else if(osPL_findStr(&qName, "_sip._tcp.", 0))
            {
                tpType = TRANSPORT_TYPE_TCP;
            }
            else
            {
                logError("srv record qName(%r) does not contain _sip._udp. or _sip._tcp.", &qName);
                status = OS_ERROR_INVALID_VALUE;
				goto EXIT;
            }

            osListElement_t* pLE = pDnsRsp->answerList.head;
            while(pLE)
            {
                dnsRR_t* pDnsRR = pLE->data;
                mdebug(LM_SIPAPP, "SRV, type=%d, rrClase=%d, ttl=%d, priority=%d, weight=%d, port=%d, target=%s", pDnsRR->type, pDnsRR->rrClass, pDnsRR->ttl, pDnsRR->srv.priority, pDnsRR->srv.weight, pDnsRR->srv.port, pDnsRR->srv.target);

                osListElement_t* pARLE = pDnsRsp->addtlAnswerList.head;
                while(pARLE)
                {
                    dnsRR_t* pARDnsRR = pARLE->data;
                    if(pARDnsRR->type != DNS_QTYPE_A)
                    {
                        continue;
                    }

                    if(strcasecmp(pARDnsRR->name, pDnsRR->srv.target) == 0)
                    {
		                sipTuDestInfo_t* pDestInfo = osmalloc(sizeof(sipTuDestInfo_t), sipTuDestInfo_cleanup);

        		        pDestInfo->tpType = tpType;
                		pDestInfo->priority = pDnsRR->srv.priority;
                		pDestInfo->weight = pDnsRR->srv.weight;
                		pDestInfo->destAddr.sin_addr = ((dnsRR_t*)pDnsRR)->ipAddr;
                		pDestInfo->destAddr.sin_port = pDnsRR->srv.port;
                		pDestInfo->destAddr.sin_family = AF_INET;

		                osList_orderAppend(&pDestRec->destList, sipTuDest_sortDestInfo, pDestInfo, NULL);
                    }
                    pARLE = pARLE->next;
                }

				pLE = pLE->next;
			}
			break;
		}
        case DNS_QTYPE_NAPTR:
        {
            osPointerLen_t qName ={pDnsRsp->query.qName, strlen(pDnsRsp->query.qName)};
            osVPL_copyPL(&pDestRec->destName, &qName);
            osListElement_t* pARLE = pDnsRsp->addtlAnswerList.head;
            while(pARLE)
            {
                dnsRR_t* pARDnsRR = pARLE->data;
                if(pARDnsRR->type == DNS_QTYPE_A)
                {
					mdebug(LM_SIPAPP, "qType=DNS_QTYPE_NAPTR, find a DNS_QTYPE_A RR, pARDnsRR->name=%s", pARDnsRR->name);
					sipTuDestSrvInfo_t srvInfo[SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM];
					int srvInfoNum = sipTuDest_msgGetSrvInfo(&pDnsRsp->addtlAnswerList, pARDnsRR->name, srvInfo);	
					for(int i=0; i<srvInfoNum; i++)
					{
						sipTuDestInfo_t* pDestInfo = sipTuDest_isDestExist(((dnsRR_t*)pARDnsRR)->ipAddr, srvInfo[i].port, srvInfo[i].priority, srvInfo[i].weight, &pDestRec->destList);
                    	if(!pDestInfo)
						{
							pDestInfo = osmalloc(sizeof(sipTuDestInfo_t), sipTuDestInfo_cleanup);
                    		pDestInfo->tpType = srvInfo[i].tpType;
                    		pDestInfo->priority = srvInfo[i].priority;
                    		pDestInfo->weight = srvInfo[i].weight;
                    		pDestInfo->destAddr.sin_addr = pARDnsRR->ipAddr;
                    		pDestInfo->destAddr.sin_port = htons(srvInfo[i].port);
                    		pDestInfo->destAddr.sin_family = AF_INET;
							mdebug(LM_SIPAPP, "pDestInfo=%p, pDestInfo->destAddr=%A, tpType=%d, priority=%d, weight=%d", pDestInfo, &pDestInfo->destAddr, pDestInfo->tpType, pDestInfo->priority, pDestInfo->weight);

			                osList_orderAppend(&pDestRec->destList, sipTuDest_sortDestInfo, pDestInfo, NULL);
						}
						else if(pDestInfo->tpType != srvInfo[i].tpType)
						{
							pDestInfo->tpType = TRANSPORT_TYPE_ANY;
							mdebug(LM_SIPAPP, "pDestInfo=%p, tpType=TRANSPORT_TYPE_ANY", pDestInfo);
						}
					}
                }
                pARLE = pARLE->next;
            }
            break;
        }
		default:
			logError("qType(%d) unexpected.", pDnsRsp->query.qType);
			status = OS_ERROR_INVALID_VALUE;
			goto EXIT;
			break;
	}

EXIT:	
	return status;
}


//if a A record showns in additional record section, all srv info is assumed to be included in the same message, otherwise, search other dns response message
static osStatus_e sipTUDest_msgListAddARec(sipTuDestRecord_t* pDestRec, dnsMessage_t* pDnsRsp, dnsResResponse_t* pRR)
{
	osStatus_e status = OS_STATUS_OK;

    switch(pDnsRsp->query.qType)
    {
        case DNS_QTYPE_A:
		{            
			osListElement_t* pLE = pDnsRsp->answerList.head;
            while(pLE)
            {
				sipTuDestSrvInfo_t srvInfo[SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM];
				for(int i=0; i<sipTuDest_msgListGetSrvInfo(pRR, pDnsRsp->query.qName, srvInfo); i++)
				{
					sipTuDestInfo_t* pDestInfo = sipTuDest_isDestExist(((dnsRR_t*)pLE->data)->ipAddr, srvInfo[i].port, srvInfo[i].priority, srvInfo[i].weight, &pDestRec->destList);
					if(!pDestInfo)
					{
						pDestInfo = osmalloc(sizeof(sipTuDestInfo_t), sipTuDestInfo_cleanup);
						pDestInfo->tpType = srvInfo[i].tpType;
						pDestInfo->priority = srvInfo[i].priority;
						pDestInfo->weight = srvInfo[i].weight;
						pDestInfo->destAddr.sin_addr = ((dnsRR_t*)pLE->data)->ipAddr;
						pDestInfo->destAddr.sin_port = srvInfo[i].port;
						pDestInfo->destAddr.sin_family = AF_INET;

		                osList_orderAppend(&pDestRec->destList, sipTuDest_sortDestInfo, pDestInfo, NULL);
					}
					else if(pDestInfo->tpType != srvInfo[i].tpType)
					{
						pDestInfo->tpType = TRANSPORT_TYPE_ANY;
					}

					pLE = pLE->next;
				}
			}
			break;
		}
		case DNS_QTYPE_SRV:
			status = sipTuDest_msgAddRec(pDnsRsp, pDestRec);
			break;
		case DNS_QTYPE_NAPTR:
		default:
			logError("pDnsRsp->query.qType=%d is not handled.", pDnsRsp->query.qType);
			status = OS_ERROR_INVALID_VALUE;
			break;
	}

EXIT:
	return status;
}



static dnsMessage_t* sipTuDest_getRRInfo(dnsResResponse_t* pRR, osPointerLen_t* qName, bool* isNoError)
{
	dnsMessage_t* pTopDnsRsp = NULL;	//the dns response of the highest qType contained in pRR
	dnsQType_e highestQType = 0;
	*isNoError = true;

	if(!pRR)
	{
		logError("null pointer, pRR.");
		goto EXIT;
	}

	switch(pRR->rrType)
	{
		case DNS_RR_DATA_TYPE_MSG:
			*isNoError = (pRR->pDnsRsp->hdr.flags & DNS_RCODE_MASK) == DNS_RCODE_NO_ERROR;
			pTopDnsRsp = pRR->pDnsRsp;
			break;
		case DNS_RR_DATA_TYPE_MSGLIST:
		{
			osListElement_t* pRRLE = pRR->dnsRspList.head;
            while(pRRLE)
            {
                dnsMessage_t* pDnsRsp = pRRLE->data;
				*isNoError = *isNoError ? (pDnsRsp->hdr.flags & DNS_RCODE_MASK) == DNS_RCODE_NO_ERROR : *isNoError;
				if(highestQType < pDnsRsp->query.qType)
				{
					highestQType = pDnsRsp->query.qType;
					pTopDnsRsp = pDnsRsp;
				}

				pRRLE = pRRLE->next;
			}
			break;
		}
		default:
			goto EXIT;
	}

	if(pTopDnsRsp)
	{
		osPL_setStr(qName, pTopDnsRsp->query.qName, 0);
	}

EXIT:
	return pTopDnsRsp;
}


static sipTuDestInfo_t* sipTuDest_isDestExist(struct in_addr ipAddr, int port, int priority, int weight, osList_t* pDestList)
{
	if(!pDestList)
	{
		logError("null pointer, pDestList.");
		return NULL;
	}

	osListElement_t* pLE = pDestList->head;
	while(pLE)
	{
		sipTuDestInfo_t* pDestInfo  = pLE->data;
		if(pDestInfo->destAddr.sin_addr.s_addr == ipAddr.s_addr && pDestInfo->destAddr.sin_port == port && pDestInfo->priority == priority && pDestInfo->weight == weight)
		{
			return pDestInfo;
			break;
		}

		pLE = pLE->next;
	}

	return NULL;
}


//pAnswerList may be dnsMessage_t.addtlAnswerList or dnsMessage_t.answerList, depends on who calling it
static int sipTuDest_msgGetSrvInfo(osList_t* pAnswerList, char* aQName, sipTuDestSrvInfo_t srvInfo[])
{
	int srvInfoNum = 0;

	if(!pAnswerList || !aQName || !srvInfo)
	{
		logError("null pointer, pAnswerList=%p, aQName=%p, srvInfo=%p.", pAnswerList, aQName, srvInfo);
		goto EXIT;
	}

	osListElement_t* pARLE = pAnswerList->head;
	while(pARLE)
	{
		dnsRR_t* pARDnsRR = pARLE->data;
		if(pARDnsRR->type == DNS_QTYPE_SRV && strcasecmp(pARDnsRR->srv.target, aQName) == 0)
		{
			osPointerLen_t srvQName = {pARDnsRR->name, strlen(pARDnsRR->name)};
			if(osPL_findStr(&srvQName, "_sip._udp.", 0))
			{
				srvInfo[srvInfoNum].tpType = TRANSPORT_TYPE_UDP;
			}
			else if(osPL_findStr(&srvQName, "_sip._tcp.", 0))
			{
				srvInfo[srvInfoNum].tpType = TRANSPORT_TYPE_TCP;
			}
			else
			{
				logError("srv record finds a matching target(%s), but the qName(%r) does not contain _sip._udp. or _sip._tcp.", aQName, &srvQName); 
				pARLE = pARLE->next;
				continue;
			}

			srvInfo[srvInfoNum].priority = pARDnsRR->srv.priority;
			srvInfo[srvInfoNum].weight = pARDnsRR->srv.weight;
			srvInfo[srvInfoNum].port = pARDnsRR->srv.port;

			mdebug(LM_SIPAPP, "srvInfoNum=%d, srv=%p, priority=%d, weight=%d, port=%d", srvInfoNum, &pARDnsRR->srv, srvInfo[srvInfoNum].priority, srvInfo[srvInfoNum].weight, pARDnsRR->srv.port);
	        if(++srvInfoNum >= SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM)
    	    {
				logError("srvInfoNum(%d) exceed SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM.  This shall not happen.", srvInfoNum);
        	    goto EXIT;
        	}
		}

		pARLE = pARLE->next;
	}

EXIT:
	return srvInfoNum;
}


//go through all dnsMessage inside pRR to find SRV that has target = aQName. 
static int sipTuDest_msgListGetSrvInfo(dnsResResponse_t* pRR, char* aQName, sipTuDestSrvInfo_t srvInfo[])
{
	int srvInfoNum = 0;

    if(!pRR || !aQName || !srvInfo)
    {
        logError("null pointer, pRR=%p, aQName=%p, srvInfo=%p.", pRR, aQName, srvInfo);
        goto EXIT;
    }

	osListElement_t* pMsgLE = pRR->dnsRspList.head;
	while(pMsgLE)
	{
		dnsMessage_t* pDnsRsp = pMsgLE->data;
		mdebug(LM_SIPAPP, "pDnsRsp=%p, pDnsRsp->query.qType=%d", pDnsRsp, pDnsRsp->query.qType);
		//if qType=NAPTR, check the addtlAnswerList if there is SRV record.  if qType = SRV, check the answerListType
		//be noted there is hole here, inside sipTuDest_msgGetSrvInfo() there is check to make sure the entries of srvInfo
		//be no more than SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM.  Since each call allows that maximum number, add them
		//together, the total number may be more than SIP_TU_DEST_MAX_SUPPORTED_TP_TYPE_NUM.  In reality, expect the maximum
		//number is 2, one for TCP, one for UDP.  make the number to be bigger just in case
		switch(pDnsRsp->query.qType)
		{
			case DNS_QTYPE_NAPTR:
				srvInfoNum += sipTuDest_msgGetSrvInfo(&pDnsRsp->addtlAnswerList, aQName, &srvInfo[srvInfoNum]);
                break;
			case DNS_QTYPE_SRV:
				srvInfoNum += sipTuDest_msgGetSrvInfo(&pDnsRsp->answerList, aQName, &srvInfo[srvInfoNum]);
				break;
			default:
				logError("unexpected pDnsRsp->query.qType(%d).", pDnsRsp->query.qType)
				break;
		}

		pMsgLE = pMsgLE->next;
	}

EXIT:
	return srvInfoNum;
}



static bool sipTuDest_sortDestInfo(osListElement_t *le1, osListElement_t *le2, void *arg)
{
	bool isSwitch = false;
	sipTuDestInfo_t* pDest1 = le1->data;
	sipTuDestInfo_t* pDest2 = le2->data;

	if(pDest1->priority > pDest2->priority)
	{
		isSwitch = true;
		goto EXIT;
	}
	else if(pDest1->priority == pDest2->priority)
	{
		if(pDest1->weight > pDest2->weight)
		{
			isSwitch = true;
		}
	}

EXIT:
	return isSwitch;
}
	
	
//implement the nexthop selection based on rfc2782
static bool sipTuDest_getNextHop(sipTuDestRecord_t* pDestRec, sipTuAddr_t* pNextHop)
{
	bool isFound = false;

	osListElement_t* pLE = pDestRec->destList.head;
	while(pLE)
	{
		sipTuDestInfo_t* pDestInfo = pLE->data;
		if(pDestInfo->isQuarantined)
		{
			pLE = pLE->next;
			continue;
		}

		//the pLE here is the highest priority(smallest value)  element, but there may have multiple elements witht he same priority, let's check
		int weightSum = 0;
		int destSamePriNum = sipTuDest_getSamePriorityNum(pLE, &weightSum);
		if(destSamePriNum == 1)
		{
			pNextHop->isSockAddr = true;
			pNextHop->sockAddr = pDestInfo->destAddr;
			pNextHop->tpType = pDestInfo->tpType;
			return true;
		}
		else
		{
			//find multiple elements with the same priority, use weight to select a element
		    struct timespec tp;
    		clock_gettime(CLOCK_REALTIME, &tp);
			int rand = tp.tv_nsec % (weightSum+1);
			int runSum = 0;
			while(pLE)
			{
				runSum += ((sipTuDestInfo_t*)pLE->data)->weight;
				if(runSum >= rand)
				{
					pNextHop->isSockAddr = true;
					pNextHop->sockAddr = ((sipTuDestInfo_t*)pLE->data)->destAddr;
					pNextHop->tpType = ((sipTuDestInfo_t*)pLE->data)->tpType;
					return true;
				}
			
				pLE = pLE->next;
			}

			logError("a Dest using weight pick shall have happened, something is wrong.");
			return false;
		}

		pLE = pLE->next;
	}

	return isFound;
}


//given the LE of the first element, find the number of elements that have the same priority and not quarantined
static int sipTuDest_getSamePriorityNum(osListElement_t* pLE, int* weightSum)
{
	int num = 0;
	*weightSum = 0;
	int priority = ((sipTuDestInfo_t*)pLE->data)->priority;
	while(pLE)
	{
		if(((sipTuDestInfo_t*)pLE->data)->priority == priority)
		{
			if(!((sipTuDestInfo_t*)pLE->data)->isQuarantined)
			{
				num++;
				*weightSum += ((sipTuDestInfo_t*)pLE->data)->weight;
			}
		}
		else
		{
			break;
		}

		pLE = pLE->next;
	}

	return num;
}


static osListElement_t* sipTuDest_getDestRec(osPointerLen_t* qName)
{
    osListElement_t* pLE = gDestRecordList.head;
    while(pLE)
    {
        sipTuDestRecord_t* pDestRec = pLE->data;
        if(osPL_cmp(&pDestRec->destName.pl, qName) == 0)
        {
            return pLE;
        }

        pLE = pLE->next;
    }

    return NULL;
}


static void sipTuDestRecord_cleanup(void* data)
{
	if(!data)
	{
		return;
	}

	sipTuDestRecord_t* pDestRec = data;

	osfree((char*)pDestRec->destName.pl.p);
	osList_delete(&pDestRec->destList);
	if(pDestRec->kaTimerId)
	{
		osStopTimer(pDestRec->kaTimerId);
	}
}


static void sipTuDestInfo_cleanup(void* data)
{
    if(!data)
    {
        return;
    }

	sipTuDestInfo_t* pDestInfo = data;
	
	if(pDestInfo->qTimerId)
	{
		osStopTimer(pDestInfo->qTimerId);
	}
}


static void sipTuDest_dbgList()
{
	mdebug(LM_SIPAPP, "dest record list:");

	int i=0;
	osListElement_t* pLE = gDestRecordList.head;
	while(pLE)
	{
		mdebug1(LM_SIPAPP, "i=%d, destName=%r\n", i++, &((sipTuDestRecord_t*)pLE->data)->destName);
		osListElement_t* pDestLE = ((sipTuDestRecord_t*)pLE->data)->destList.head;
		while(pDestLE)
		{
			sipTuDestInfo_t* pDestInfo = pDestLE->data;
			mdebug1(LM_SIPAPP, "    isQuarantined=%d, tpType=%d, priority=%d, weight=%d, destAddr=%A\n", pDestInfo->isQuarantined, pDestInfo->tpType, pDestInfo->priority, pDestInfo->weight, &pDestInfo->destAddr);
			pDestLE = pDestLE->next;
		}

		pLE = pLE->next;
	}
}    
		
