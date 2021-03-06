/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file scscfIfc.c
 * implement 3GPP 29.228 IFC parsing
 *
 * an example ifc
 * <SharedIFCSet>
 *     <SharedIFCSetID>xx</SharedIFCSetID>
 *     <InitialFilterCriteria>
 *        <Priority>0</Priority>
 *        <TriggerPoint>
 *        </TriggerPoint>
 *        <ApplicationServer>
 *        </ApplicationServer>
 *    </InitialFilterCriteria>
 *    <InitialFilterCriteria>
 *    </InitialFilterCriteria>
 * </SharedIFCSet>
 * <SharedIFCSet>
 * ...
 * </SharedIFCSet>
 *
 * CNF == true: (a or b or c) and (d or e)
 * CNF == false: (a and b and c) or (d and e)
 ********************************************************/


#include <string.h>

#include "osTypes.h"
#include "osMBuf.h"
#include "osConfig.h"

#include "sipMsgFirstLine.h"
#include "sipHeaderData.h"
#include "sipHeader.h"

#include "scscfIfc.h"
#include "scscfRegistrar.h"



typedef enum {
	SCSCF_IFC_DEFAULT_HANDLING_SESS_CONTINUED = 0,
	SCSCF_IFC_DEFAULT_HANDLING_SESS_TERMINATED = 1,
}scscfIfcDefaultHandling_e;


typedef enum {
	SCSCF_IFC_SPT_TYPE_METHOD,
	SCSCF_IFC_SPT_TYPE_REQUEST_URI,
	SCSCF_IFC_SPT_TYPE_SESSION_CASE,
	SCSCF_IFC_SPT_TYPE_HEADER,
} scscfIfcSptType_e;


typedef struct {
	sipHdrName_e hdrName;
	osPointerLen_t hdrContent;
} ifcSptHdr_t;

typedef struct {
	scscfIfcSptType_e sptType;
	bool isConditionNegated;
	uint8_t regTypeMap;		//each one of the three regType occupies one bit, 1<<reg_type
	union {
		osPointerLen_t reqUri;
		sipRequest_e method;
		ifcSptHdr_t header;
		scscfIfcSessCase_e sessCase;
	};
} scscfIfcSptInfo_t;


typedef struct {
	osList_t sptGrp;	//each element contains a scscfIfcSptInfo_t.  all element in the sptGrp has the same group value
} scscfIfcSptGrp_t;
		
typedef struct {
    int sIfcGrpId; 		//a sifc group id(effectively a service profile), multiple sIfc may share one group Id.  The default value = -1.  This is NOT part of IFC
	bool conditionTypeCNF;
	uint32_t priority;
	osList_t sptGrpList;	//each entry contains a scscfIfcSptGrp_t
	osPointerLen_t asName;
	bool isDefaultSessContinued;	//default handling
} scscfIfc_t;	


typedef enum {
	SCSCF_IFC_SPT,
	SCSCF_IFC_Group,
	SCSCF_IFC_Method,
	SCSCF_IFC_Header,
	SCSCF_IFC_Content,
	SCSCF_IFC_Priority,
	SCSCF_IFC_SIPHeader,
	SCSCF_IFC_RequestURI,
	SCSCF_IFC_ServerName,
	SCSCF_IFC_SessionCase,
    SCSCF_IFC_TriggerPoint,
	SCSCF_IFC_SharedIFCSetID,
	SCSCF_IFC_DefaultHandling,
	SCSCF_IFC_ConditionNegated,
	SCSCF_IFC_ConditionTypeCNF,
	SCSCF_IFC_RegistrationType,	
	SCSCF_IFC_InitialFilterCriteria,
    SCSCF_IFC_XML_MAX_DATA_NAME_NUM,
} scscfIfc_xmlDataName_e;


osXmlData_t scscfIfc_xmlData[SCSCF_IFC_XML_MAX_DATA_NAME_NUM] = {
    {SCSCF_IFC_SPT,		{"SPT", sizeof("SPT")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
    {SCSCF_IFC_Group,	{"Group", sizeof("Group")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_Method,	{"Method", sizeof("Method")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_Header,	{"Header", sizeof("Header")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_Content,	{"Content", sizeof("Content")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_Priority,	{"Priority", sizeof("Priority")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_SIPHeader,	{"SIPHeader", sizeof("SIPHeader")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
    {SCSCF_IFC_RequestURI,	{"RequestURI", sizeof("RequestURI")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_ServerName,	{"ServerName", sizeof("ServerName")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_SessionCase,	{"SessionCase", sizeof("SessionCase")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_TriggerPoint,	{"TriggerPoint", sizeof("TriggerPoint")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
	{SCSCF_IFC_SharedIFCSetID,	{"SharedIFCSetID", sizeof("SharedIFCSetID")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_DefaultHandling,	{"DefaultHandling", sizeof("DefaultHandling")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_ConditionNegated,	{"ConditionNegated", sizeof("ConditionNegated")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_ConditionTypeCNF,	{"ConditionTypeCNF", sizeof("ConditionTypeCNF")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_RegistrationType, 	{"RegistrationType", sizeof("RegistrationType")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_IFC_InitialFilterCriteria, {"InitialFilterCriteria", sizeof("InitialFilterCriteria")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
}; 


static void scscfIfc_parseSIfcCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData);
static bool scscfIfcSptGrpIsMatch(scscfIfcSptGrp_t* pSptGrp, bool isOr, sipMsgDecodedRawHdr_t* pReqDecodedRaw, scscfIfcEvent_t* pIfcEvent);
static bool scscfIfc_isIdMatch(sIfcIdList_t* pSIfcIdList, uint32_t sIfcGrpId);
static bool scscfIfc_sortPriority(osListElement_t *le1, osListElement_t *le2, void *arg);
static osStatus_e scscfIfc_constructXml(osMBuf_t* origSIfcXmlBuf);
static void scscfIfc_dbgList(osList_t* pSIfcSet);


static osList_t gSIfcSet;	//each entry contains a scscfIfc_t
static osMBuf_t* gSIfcXmlBuf = NULL;	//shall be freed/replaced when ifcXml changes



osStatus_e scscfIfc_init(char* sIfcFileFolder, char* sIfcXsdFileName, char* sIfcXmlFileName)
{
    osStatus_e status = OS_STATUS_OK;
	osMBuf_t* sIfcXsdBuf = NULL;

    if(!sIfcXsdFileName || !sIfcXmlFileName)
    {
        logError("null pointer, sIfcXsdFileName=%p, sIfcXmlFileName=%p.", sIfcXsdFileName, sIfcXmlFileName);
        status = OS_ERROR_NULL_POINTER;
        goto EXIT;
    }

    char sIfcXmlFile[OS_MAX_FILE_NAME_SIZE];

    if(snprintf(sIfcXmlFile, OS_MAX_FILE_NAME_SIZE, "%s/%s", sIfcFileFolder ? sIfcFileFolder : ".", sIfcXmlFileName) >= OS_MAX_FILE_NAME_SIZE)
    {
        logError("sIfcXmlFile name is truncated.");
        status = OS_ERROR_INVALID_VALUE;
    }

    //8000 is the initial mBuf size.  If the reading needs more than 8000, the function will realloc new memory
    osMBuf_t* pSIfcOrigXmlBuf = osMBuf_readFile(sIfcXmlFile, 8000);
    if(!pSIfcOrigXmlBuf)
    {
        logError("read gSIfcXmlBuf fails, sIfcXmlFile=%s", sIfcXmlFile);
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	status = scscfIfc_constructXml(pSIfcOrigXmlBuf);
	if(status != OS_STATUS_OK)
	{
		logError("fails to construct sIfcXml.");
        status = OS_ERROR_SYSTEM_FAILURE;
        goto EXIT;
    }

	osPointerLen_t xsdName = {sIfcXsdFileName, strlen(sIfcXsdFileName)};
    osXmlDataCallbackInfo_t cbInfo={true, false, false, scscfIfc_parseSIfcCB, &gSIfcSet, scscfIfc_xmlData, SCSCF_IFC_XML_MAX_DATA_NAME_NUM};
    osXml_getElemValue(&xsdName, NULL, gSIfcXmlBuf, true, &cbInfo);

    //sort ifc based on priority
    osList_sort(&gSIfcSet, scscfIfc_sortPriority, NULL);

	scscfIfc_dbgList(&gSIfcSet);

EXIT:
	if(status != OS_STATUS_OK)
	{
	   //if status == OK, keeps gSIfcXmlBuf, as some data structure (osPL_t) in the callback will refer it.  As long as th application wants to use the data structure in th callback, it shall keep it.
		osfree(gSIfcXmlBuf);
	}

	return status;
}


osPointerLen_t* scscfIfc_getNextAS(osListElement_t** ppLastSIfc, sIfcIdList_t* pSIfcIdList, sipMsgDecodedRawHdr_t* pReqDecodedRaw, scscfIfcEvent_t* pIfcEvent, bool* isContinueDH)
{
	osPointerLen_t* pAs = NULL;
	if(!ppLastSIfc || !pSIfcIdList || !pIfcEvent)
	{
		logError("null pointer, ppLastSIfc=%p, pSIfcIdList=%p, pIfcEvent=%p.", ppLastSIfc, pSIfcIdList, pIfcEvent);
		goto EXIT;
	}

	mdebug(LM_CSCF, "ifc event: sipMethod=%d, sessCase=%d, regType=%d", pIfcEvent->sipMethod, pIfcEvent->sessCase, pIfcEvent->regType);

	osListElement_t* pLE = *ppLastSIfc ? (*ppLastSIfc)->next : gSIfcSet.head;
	while(pLE)
	{
		*ppLastSIfc = pLE;

		scscfIfc_t* pIfc = pLE->data;
		if(!scscfIfc_isIdMatch(pSIfcIdList, pIfc->sIfcGrpId))
		{
			pLE = pLE->next;
			continue;
		}

		mdebug(LM_CSCF, "a matching IFC(%p) is found, sIfcGrpId=%d, isDefaultSessContinued=%d, conditionTypeCNF=%d", pIfc, pIfc->sIfcGrpId, pIfc->isDefaultSessContinued, pIfc->conditionTypeCNF);

		osListElement_t* pSptGrpLE = pIfc->sptGrpList.head;

        //if an IFC has empty SPT, directly use AS
        if(!pSptGrpLE)
        {
            mdebug(LM_CSCF, "the IFC(%p) does not have SPT, just use the AS(%r).", pIfc, &pIfc->asName);
            pAs = &pIfc->asName;
			*isContinueDH = pIfc->isDefaultSessContinued;
            goto EXIT;
        }

		//there is SPT inside the IFC, check the conditions
		while(pSptGrpLE)
		{
			if(pIfc->conditionTypeCNF)
			{
				if(!scscfIfcSptGrpIsMatch(pSptGrpLE->data, true, pReqDecodedRaw, pIfcEvent))
				{
					mdebug(LM_CSCF, "conditionTypeCNF, SPT no match, fails this IFC(%p).", pIfc); 
					break;
				}
			}
			else
			{
				if(scscfIfcSptGrpIsMatch(pSptGrpLE->data, false, pReqDecodedRaw, pIfcEvent))
				{
					mdebug(LM_CSCF, "not conditionTypeCNF, SPT match, find the IFC(%p), AS=%r.", pIfc, &pIfc->asName);
					pAs = &pIfc->asName;
		            *isContinueDH = pIfc->isDefaultSessContinued;
					goto EXIT;
				}
			}

			pSptGrpLE = pSptGrpLE->next;
		}

		//when getting here, for pIfc->conditionTypeCNF, all conditions meet. for !pIfc->conditionTypeCNF, the ifc match fails, need to go to next ifc
		if(pIfc->conditionTypeCNF)
		{
			mdebug(LM_CSCF, "conditionTypeCNF, all SPTs match for the IFC(%p), use the AS(%r).", pIfc, &pIfc->asName);
			pAs = &pIfc->asName;
			*isContinueDH = pIfc->isDefaultSessContinued;
			goto EXIT;
		}

		pLE = pLE->next;
	}

	mdebug(LM_CSCF, "does not find any matching IFC.");

EXIT:
	return pAs;
}


static bool scscfIfcSptGrpIsMatch(scscfIfcSptGrp_t* pSptGrp, bool isOr, sipMsgDecodedRawHdr_t* pReqDecodedRaw, scscfIfcEvent_t* pIfcEvent)
{
	bool isMatch = true;

	osListElement_t* pLE = pSptGrp->sptGrp.head;
	if(isOr)
	{
		while(pLE)
		{
			scscfIfcSptInfo_t* pSpt = pLE->data;
			switch(pSpt->sptType)
			{
				case SCSCF_IFC_SPT_TYPE_METHOD:
					if((pSpt->method == pIfcEvent->sipMethod && !pSpt->isConditionNegated) || (pSpt->method != pIfcEvent->sipMethod && pSpt->isConditionNegated))
					{
						if(pSpt->method == SIP_METHOD_REGISTER)
						{
							if(((pSpt->regTypeMap & 1<<pIfcEvent->regType) && !pSpt->isConditionNegated) || !((pSpt->regTypeMap & 1<<pIfcEvent->regType) && pSpt->isConditionNegated))
							{
								isMatch = true;
							}
						}
						else
						{
							isMatch = true;
						}
						goto EXIT;
					}
					break;			
				case SCSCF_IFC_SPT_TYPE_REQUEST_URI:
				case SCSCF_IFC_SPT_TYPE_SESSION_CASE:
					if((pSpt->sessCase == pIfcEvent->sessCase && !pSpt->isConditionNegated) || (pSpt->sessCase != pIfcEvent->sessCase && pSpt->isConditionNegated))
					{
						isMatch = true;
                        goto EXIT;
                    }
					break;
				case SCSCF_IFC_SPT_TYPE_HEADER:
				default:
					break;
			}

			pLE = pLE->next;
		}
		isMatch = false;
	}
	else
	{
        while(pLE)
        {
            scscfIfcSptInfo_t* pSpt = pLE->data;
            switch(pSpt->sptType)
            {
                case SCSCF_IFC_SPT_TYPE_METHOD:
                    if((pSpt->method == pIfcEvent->sipMethod && pSpt->isConditionNegated) || (pSpt->method != pIfcEvent->sipMethod && !pSpt->isConditionNegated))
                    {
                        isMatch = false;
                        goto EXIT;
                    }

					//for REGISTER, one more check
                    if(pSpt->method == SIP_METHOD_REGISTER)
                    {
                        if(((pSpt->regTypeMap & 1<<pIfcEvent->regType) && pSpt->isConditionNegated) || !((pSpt->regTypeMap & 1<<pIfcEvent->regType) && !pSpt->isConditionNegated))
                        {
                            isMatch = false;
							goto EXIT;
                        }
					}
                    break;
                case SCSCF_IFC_SPT_TYPE_REQUEST_URI:
                case SCSCF_IFC_SPT_TYPE_SESSION_CASE:
                    if((pSpt->sessCase == pIfcEvent->sessCase && pSpt->isConditionNegated) || (pSpt->sessCase != pIfcEvent->sessCase && !pSpt->isConditionNegated))
                    {
                        isMatch = false;
                        goto EXIT;
                    }
                    break;
                case SCSCF_IFC_SPT_TYPE_HEADER:
                default:
                    break;
            }

            pLE = pLE->next;
        }
        isMatch = true;
    }

EXIT:
	return isMatch;
}
		

#define SCSCFIFC_DEFAULT_SIFC_ID	-1	//implies a sifc has not been assigned a sharedIfcId
static void scscfIfc_parseSIfcCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData)
{
    if(!pXmlValue)
    {
        logError("null pointer, pXmlValue.");
        return;
    }

	osList_t* pSIfcSet = appData;

    static __thread scscfIfc_t* fpIfc = NULL;
	static __thread scscfIfcSptGrp_t* fpCurSptGrp = NULL;
	static __thread scscfIfcSptInfo_t* fpCurSpt = NULL;
	static __thread ifcSptHdr_t* fpCurHdr = NULL;
	static __thread int fCurGrp = -1;

    switch(pXmlValue->eDataName)
    {
		case SCSCF_IFC_SharedIFCSetID:
		{
			//SCSCF_IFC_SharedIFCSetID shall happen after IFC.  Go through the IFC list starting from the back to assign sharedIfcId until a IFC has already been assigned a no default value  
			osListElement_t* pLE = pSIfcSet->tail;
			while(pLE)
			{
				if(((scscfIfc_t*)pLE->data)->sIfcGrpId == SCSCFIFC_DEFAULT_SIFC_ID)
				{
					((scscfIfc_t*)pLE->data)->sIfcGrpId = pXmlValue->xmlInt;
		
					mdebug(LM_CSCF, "dataName=%r, IFC(%p) is assigned SIFC id=%d", &scscfIfc_xmlData[SCSCF_IFC_SharedIFCSetID].dataName, pLE->data, pXmlValue->xmlInt);	
				}

				pLE = pLE->prev;
			}

			break;	
		}
		case SCSCF_IFC_InitialFilterCriteria:
			if(pXmlValue->isEOT)
			{
				//assign a default sharedIfcId(-1) to the IFC.  The real SIfcId will be assigned when SCSCF_IFC_SharedIFCSetID is parsed
				fpIfc->sIfcGrpId = SCSCFIFC_DEFAULT_SIFC_ID;
				osList_append(pSIfcSet, fpIfc);

				mdebug(LM_CSCF, "dataName=%r, close a IFC(%p)", &scscfIfc_xmlData[SCSCF_IFC_InitialFilterCriteria].dataName, fpIfc);
				fpIfc = NULL;
				return;
			}

			if(fpIfc)
			{
				logError("starts to parse a new ifc, but the previous ifc has not closed.");
				return;
			}

			fpIfc = oszalloc(sizeof(scscfIfc_t), NULL);
			fpIfc->isDefaultSessContinued = true;

            mdebug(LM_CSCF, "dataName=%r, start a new IFC(%p)", &scscfIfc_xmlData[SCSCF_IFC_InitialFilterCriteria].dataName, fpIfc);
			break;
		case SCSCF_IFC_Priority:
			if(!fpIfc)
			{
				logError("fpIfc is NULL for SCSCF_IFC_Priority.");
				return;
			}
			fpIfc->priority = pXmlValue->xmlInt;

            mdebug(LM_CSCF, "dataName=%r, IFC(%p) priority=%d", &scscfIfc_xmlData[SCSCF_IFC_Priority].dataName, fpIfc, fpIfc->priority);
			break;
		case SCSCF_IFC_ConditionTypeCNF:
			if(!fpIfc)
            {
                logError("fpIfc is NULL for SCSCF_IFC_ConditionTypeCNF.");
                return;
            }
			fpIfc->conditionTypeCNF = pXmlValue->xmlInt;

            mdebug(LM_CSCF, "dataName=%r, IFC(%p) conditionTypeCNF=%d", &scscfIfc_xmlData[SCSCF_IFC_ConditionTypeCNF].dataName, fpIfc, fpIfc->conditionTypeCNF);
			break;
		case SCSCF_IFC_SPT:
			if(pXmlValue->isEOT)
			{
				osList_append(&fpCurSptGrp->sptGrp, fpCurSpt);
                mdebug(LM_CSCF, "dataName=%r, close a SPT(%p)", &scscfIfc_xmlData[SCSCF_IFC_SPT].dataName, fpCurSpt);

				if(!fpCurSpt->regTypeMap)
				{
					fpCurSpt->regTypeMap = 0x7;	//if there is no regType configured for this SPT, set to match all
				}
				fpCurSpt = NULL;
			}
			else
			{
				fpCurSpt = osmalloc(sizeof(scscfIfcSptInfo_t), NULL);
				fpCurSpt->regTypeMap = 0;
	            mdebug(LM_CSCF, "dataName=%r, start a new SPT(%p)", &scscfIfc_xmlData[SCSCF_IFC_SPT].dataName, fpCurSpt);
			}

			break;
		case SCSCF_IFC_ConditionNegated:
			fpCurSpt->isConditionNegated = pXmlValue->xmlInt > 0 ? true : false;			

			mdebug(LM_CSCF, "dataName=%r, SPT(%p) isConditionNegated=%d", &scscfIfc_xmlData[SCSCF_IFC_ConditionNegated].dataName, fpCurSpt, fpCurSpt->isConditionNegated);
			break;
		case SCSCF_IFC_RegistrationType:
			if(pXmlValue->xmlInt > SCSCF_IFC_REGISTRATION_TYPE_DEREG)
			{
				logError("the SPT registration type value(%d) exceeds the max allowed(%d).", pXmlValue->xmlInt, SCSCF_IFC_REGISTRATION_TYPE_DEREG);
			}
			else
			{
				fpCurSpt->regTypeMap |= 1 << pXmlValue->xmlInt;
			}

            mdebug(LM_CSCF, "dataName=%r, SPT(%p) regType value=%d, SPT regTypemap=0x%x", &scscfIfc_xmlData[SCSCF_IFC_RegistrationType].dataName, fpCurSpt, pXmlValue->xmlInt, fpCurSpt->regTypeMap);
			break;
		case SCSCF_IFC_Group:
			//assume all SPTs with the same group value are continuous
			if(fCurGrp != pXmlValue->xmlInt)
			{
				if(fpCurSptGrp)
				{
					osList_append(&fpIfc->sptGrpList, fpCurSptGrp);
				}
				fpCurSptGrp = oszalloc(sizeof(scscfIfcSptGrp_t), NULL);
				fCurGrp = pXmlValue->xmlInt;
			}

            mdebug(LM_CSCF, "dataName=%r, IFC(%p), SPT group(%p) groupId=%d", &scscfIfc_xmlData[SCSCF_IFC_Group].dataName, fpIfc, fpCurSptGrp, fCurGrp);
			break;
		case SCSCF_IFC_TriggerPoint:
            if(pXmlValue->isEOT)
            {
                //if there is sptGrp remaining, push into the ifc's sptGrpList, and reset fpCurSptGrp, fpCurSpt, fCurGrp (they shall be initiated for each ifc)
				if(fpCurSptGrp)
				{
					osList_append(&fpIfc->sptGrpList, fpCurSptGrp);
					fpCurSptGrp = NULL;
					fpCurSpt = NULL;
					fCurGrp = -1;

	                mdebug(LM_CSCF, "dataName=%r, IFC(%p), SCSCF_IFC_TriggerPoint closes, SPT group(%p) is added into fpIfc->sptGrpList.", &scscfIfc_xmlData[SCSCF_IFC_TriggerPoint].dataName, fpIfc, fpCurSptGrp);
				}
			}
			break;
		case SCSCF_IFC_Method:
			fpCurSpt->sptType = SCSCF_IFC_SPT_TYPE_METHOD;
			fpCurSpt->method =  sipMsg_method2Code(&pXmlValue->xmlStr);

			mdebug(LM_CSCF, "dataName=%r, SPT(%p) sptType=%d, method=%d(%r)", &scscfIfc_xmlData[SCSCF_IFC_Method].dataName, fpCurSpt, fpCurSpt->sptType, fpCurSpt->method, &pXmlValue->xmlStr);
			break;
		case SCSCF_IFC_SIPHeader:
			if(pXmlValue->isEOT)
			{
				fpCurHdr = NULL;
			}
			else
			{
				fpCurSpt->sptType = SCSCF_IFC_SPT_TYPE_HEADER;
				fpCurHdr = &fpCurSpt->header;

				mdebug(LM_CSCF, "dataName=%r, SPT(%p) sptType=%d, SIPHeader(%p)", &scscfIfc_xmlData[SCSCF_IFC_SIPHeader].dataName, fpCurSpt, fpCurSpt->sptType, fpCurHdr);
			}
			break;
		case SCSCF_IFC_Header:
			fpCurHdr->hdrName = sipHdr_getNameCode(&pXmlValue->xmlStr);

			mdebug(LM_CSCF, "dataName=%r, SIPHeader(%p), hdrName=%d(%r)", &scscfIfc_xmlData[SCSCF_IFC_Header].dataName, fpCurHdr, fpCurHdr->hdrName, &pXmlValue->xmlStr);
			break;
		case SCSCF_IFC_Content:
			fpCurHdr->hdrContent = pXmlValue->xmlStr;

            mdebug(LM_CSCF, "dataName=%r, SIPHeader(%p), hdrContent=%r", &scscfIfc_xmlData[SCSCF_IFC_Header].dataName, fpCurHdr, &fpCurHdr->hdrContent);
			break;
		case SCSCF_IFC_RequestURI:
			fpCurSpt->sptType = SCSCF_IFC_SPT_TYPE_REQUEST_URI;
			fpCurSpt->reqUri = pXmlValue->xmlStr;

			mdebug(LM_CSCF, "dataName=%r, SPT(%p), type=%d, reqUri=%r", &scscfIfc_xmlData[SCSCF_IFC_RequestURI].dataName, fpCurSpt, fpCurSpt->sptType, &fpCurSpt->reqUri );
			break;
		case SCSCF_IFC_SessionCase:
			fpCurSpt->sptType = SCSCF_IFC_SPT_TYPE_SESSION_CASE;
			fpCurSpt->sessCase = pXmlValue->xmlInt >= SCSCF_IFC_SESS_CASE_ORIGINATING && pXmlValue->xmlInt <= SCSCF_IFC_SESS_CASE_ORIG_CDIV ? pXmlValue->xmlInt : SCSCF_IFC_SESS_CASE_INVALID;

			mdebug(LM_CSCF, "dataName=%r, SPT(%p), type=%d, sessCase=%d", &scscfIfc_xmlData[SCSCF_IFC_SessionCase].dataName, fpCurSpt, fpCurSpt->sptType, fpCurSpt->sessCase);
			break;
		case SCSCF_IFC_ServerName:
			fpIfc->asName = pXmlValue->xmlStr;

			mdebug(LM_CSCF, "dataName=%r, IFC(%p), asName=%r", &scscfIfc_xmlData[SCSCF_IFC_ServerName].dataName, fpIfc, &fpIfc->asName);
			break;
		case SCSCF_IFC_DefaultHandling:
			fpIfc->isDefaultSessContinued = pXmlValue->xmlInt == SCSCF_IFC_DEFAULT_HANDLING_SESS_CONTINUED ? true : false;

			mdebug(LM_CSCF, "dataName=%r, IFC(%p), isDefaultSessContinued=%d", &scscfIfc_xmlData[SCSCF_IFC_DefaultHandling].dataName, fpIfc, &fpIfc->isDefaultSessContinued);
			break;
		default:
			mlogInfo(LM_CSCF, "pXmlValue->eDataName(%d) is not processed.", pXmlValue->eDataName);
			break;
	}

	mdebug(LM_CSCF, "gSIfcSet IFC count=%d", osList_getCount(&gSIfcSet));
 
	return;
}



scscfIfcRegType_e scscfIfc_mapSar2IfcRegType(scscfRegSarRegType_e sarRegType)
{
	switch(sarRegType)
	{
    	case SCSCF_REG_SAR_RE_REGISTER:
			return SCSCF_IFC_REGISTRATION_TYPE_REREG;
			break;
    	case SCSCF_REG_SAR_DE_REGISTER:
			return SCSCF_IFC_REGISTRATION_TYPE_DEREG;
			break;
        case SCSCF_REG_SAR_REGISTER:
    	case SCSCF_REG_SAR_UN_REGISTER:
        case SCSCF_REG_SAR_INVALID:
		default:
			break;
	}

	return SCSCF_IFC_REGISTRATION_TYPE_INITIAL;
}


static bool scscfIfc_isIdMatch(sIfcIdList_t* pSIfcIdList, uint32_t sIfcGrpId)
{
	for(int i=0; i<pSIfcIdList->sIfcIdNum; i++)
	{
		if(pSIfcIdList->sIfcId[i] == sIfcGrpId)
		{
			return true;
		}
	}

	return false;
}


static bool scscfIfc_sortPriority(osListElement_t *le1, osListElement_t *le2, void *arg)
{
    bool isSwitch = false;
	scscfIfc_t* pIfc1 = le1->data;
	scscfIfc_t* pIfc2 = le2->data;

    if(pIfc1->priority < pIfc2->priority)
    {
        isSwitch = true;
        goto EXIT;
    }

EXIT:
    return isSwitch;
}


static osStatus_e scscfIfc_constructXml(osMBuf_t* origSIfcXmlBuf)
{
	osStatus_e status = OS_STATUS_OK;
    static int extensionTagLen = sizeof("</Extension")-1;
	static int sIfcSetTagLen = sizeof("</SharedIFCSetID")-1;
	bool isTagExtensionFound = false;

	if(!origSIfcXmlBuf)
	{
		logError("null pointer, origSIfcXmlBuf.");
		return OS_ERROR_NULL_POINTER;
	}

	osMBuf_dealloc(gSIfcXmlBuf);
	
	//double the original sifc xml size
	gSIfcXmlBuf = osMBuf_alloc(MAX(8000, origSIfcXmlBuf->end *2));
	osMBuf_writeBuf(gSIfcXmlBuf, SIFC_XSD_PREFIX, sizeof(SIFC_XSD_PREFIX)-1, true);

	size_t origPos = 0;
	while(origSIfcXmlBuf->pos < origSIfcXmlBuf->end)
	{
		//look for <Extension> </Extension> that wraps SharedIFCSetID element
		if((origSIfcXmlBuf->end - origSIfcXmlBuf->pos) > sIfcSetTagLen && (OSMBUF_CHAR(origSIfcXmlBuf, 0) == '<' && OSMBUF_CHAR(origSIfcXmlBuf, 1) == '/' && OSMBUF_CHAR(origSIfcXmlBuf, 2) == 'S'))
		{
			if(strncasecmp(&OSMBUF_CHAR(origSIfcXmlBuf, 0), "</SharedIFCSetID", sIfcSetTagLen) == 0 && (OSMBUF_CHAR(origSIfcXmlBuf, sIfcSetTagLen) == '>' || OSMBUF_CHAR(origSIfcXmlBuf, sIfcSetTagLen) == ' ' || OSMBUF_CHAR(origSIfcXmlBuf, sIfcSetTagLen) == 0x09))
			{
				origSIfcXmlBuf->pos += sIfcSetTagLen;
				osMBuf_skipUntil(origSIfcXmlBuf, '<');
		
				//after identified </SharedIFCSetID>, search for </Extension>		
				if((origSIfcXmlBuf->end - origSIfcXmlBuf->pos) > extensionTagLen && (OSMBUF_CHAR(origSIfcXmlBuf, 0) == '<' && OSMBUF_CHAR(origSIfcXmlBuf, 1) == '/' && OSMBUF_CHAR(origSIfcXmlBuf, 2) == 'E'))
				{

					if(strncasecmp(&OSMBUF_CHAR(origSIfcXmlBuf, 0), "</Extension", extensionTagLen) == 0 && (OSMBUF_CHAR(origSIfcXmlBuf, extensionTagLen) == '>' || OSMBUF_CHAR(origSIfcXmlBuf, extensionTagLen) == ' ' || OSMBUF_CHAR(origSIfcXmlBuf, extensionTagLen) == 0x09))
					{
						origSIfcXmlBuf->pos += extensionTagLen;
						isTagExtensionFound = true;
						mdebug(LM_CSCF, "isTagExtensionFound=%d, pos=%ld", isTagExtensionFound, origSIfcXmlBuf->pos);
					}
				}
			}
		}
		else if(isTagExtensionFound && OSMBUF_CHAR(origSIfcXmlBuf, 0) == '<')
		{
			osPointerLen_t origSIfcXml = {&origSIfcXmlBuf->buf[origPos], origSIfcXmlBuf->pos - origPos};
			osMBuf_writePL(gSIfcXmlBuf, &origSIfcXml, true);
			osMBuf_writeBuf(gSIfcXmlBuf, SIFC_XSD_MIDFIX, sizeof(SIFC_XSD_MIDFIX)-1, true);
            mdebug(LM_CSCF, "insert the IFC, origPos=%ld, endPos=%ld", origPos, origSIfcXmlBuf->pos);

			origPos = origSIfcXmlBuf->pos;
			isTagExtensionFound = false;
		}
					
		origSIfcXmlBuf->pos++;
	}

	osPointerLen_t origSIfcXml = {&origSIfcXmlBuf->buf[origPos], origSIfcXmlBuf->pos - origPos};
	osMBuf_writePL(gSIfcXmlBuf, &origSIfcXml, true);
	mdebug(LM_CSCF, "insert last IFC, origPos=%ld, endPos=%ld", origPos, origSIfcXmlBuf->pos);

	osMBuf_writeBuf(gSIfcXmlBuf, SIFC_XSD_SUFFIX, sizeof(SIFC_XSD_SUFFIX)-1, true);

	mdebug(LM_CSCF, "gSIfcXmlBuf=\n%M", gSIfcXmlBuf);
EXIT:
	return status;
}


scscfIfcSessCase_e scscfIfc_getSessCase(scscfRegState_e regState, bool isMO)
{
	scscfIfcSessCase_e sessCase = SCSCF_IFC_SESS_CASE_INVALID;

	switch(regState)
	{
		case SCSCF_REG_STATE_UN_REGISTERED:
			if(isMO)
			{
				sessCase = SCSCF_IFC_SESS_CASE_ORIG_UNREGISTERED;
			}
			else
			{
				sessCase = SCSCF_IFC_SESS_CASE_TERM_UNREGISTERED;
			}
			break;
		case SCSCF_REG_STATE_REGISTERED:  
			if(isMO)
			{
				sessCase = SCSCF_IFC_SESS_CASE_ORIGINATING;
			}
			else
			{
				sessCase = SCSCF_IFC_SESS_CASE_TERM_REGISTERED;
			}
			break;
		default:
			break;
	}

	return sessCase;
}


static void scscfIfc_dbgList(osList_t* pSIfcSet)
{
	if(!pSIfcSet)
	{
		return;
	}

    mdebug(LM_CSCF, "scscf sifc:");

	int i=0;
	osListElement_t* pLE = pSIfcSet->head;
	while(pLE)
	{
		scscfIfc_t* pSIfc = pLE->data;
		int j=0;
	
		mdebug1(LM_CSCF, "sifc-%d(%p)\n", i++, pSIfc);
		mdebug1(LM_CSCF, "    sifc groupId: %d\n", pSIfc->sIfcGrpId);
		mdebug1(LM_CSCF, "    conditionTypeCNF: %d\n", pSIfc->conditionTypeCNF);
		mdebug1(LM_CSCF, "    priority: %d\n", pSIfc->priority);
		mdebug1(LM_CSCF, "    isDefaultSessContinued: %d\n", pSIfc->isDefaultSessContinued);
		mdebug1(LM_CSCF, "    asName: %r\n", &pSIfc->asName);

		osListElement_t* pSptGrpLE = pSIfc->sptGrpList.head;
		while(pSptGrpLE)
		{
            scscfIfcSptGrp_t* pSptGrp = pSptGrpLE->data;
			int k=0;
			mdebug1(LM_CSCF, "    spt group-%d(%p)\n", k++, pSptGrp);
			osListElement_t* pSptLE = pSptGrp->sptGrp.head;
			int l=0;
			while(pSptLE)
			{ 
				scscfIfcSptInfo_t* pSpt = pSptLE->data;
				mdebug1(LM_CSCF, "        spt-%d(%p)\n", l++, pSpt);
                mdebug1(LM_CSCF, "        isConditionNegated=%d\n", pSpt->isConditionNegated);
				mdebug1(LM_CSCF, "        sptType=%d\n", pSpt->sptType);
				switch(pSpt->sptType)
				{
				    case SCSCF_IFC_SPT_TYPE_METHOD:
						mdebug1(LM_CSCF, "            method=%d, regTypeMap=0x%x\n\n", pSpt->method, pSpt->regTypeMap);
						break;
    				case SCSCF_IFC_SPT_TYPE_REQUEST_URI:
						mdebug1(LM_CSCF, "            reqUri=%r\n\n", &pSpt->reqUri);
						break;
    				case SCSCF_IFC_SPT_TYPE_SESSION_CASE:
						mdebug1(LM_CSCF, "            sessCase=%d\n\n", pSpt->sessCase);
						break;
    				case SCSCF_IFC_SPT_TYPE_HEADER:
						mdebug1(LM_CSCF, "            hdrName=%d, hdrContent=%r\n\n", pSpt->header.hdrName, &pSpt->header.hdrContent);
						break;
					default:
						logError("sptType(%d) is unexpected.", pSpt->sptType);
						break;
				}

				pSptLE = pSptLE->next;
			}

			pSptGrpLE = pSptGrpLE->next;
		}

		pLE = pLE->next;
	}

	return;
}
