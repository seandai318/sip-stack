/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file cscfConfig.c
 ********************************************************/

#include <string.h>

#include "osPL.h"
#include "osMBuf.h"
#include "osSockAddr.h"
#include "osMemory.h"

#include "cscfConfig.h"
#include "scscfRegistrar.h"



static void scscfConfig_userProfileCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData);
static void cscfConfig_setGlobalSockAddr();


static osPointerLen_t gCxXsdName;
static struct sockaddr_in gScscfSockAddr, gIcscfSockAddr;
static scscfAddrInfo_t gScscfAddrInfo[ICSCF_CONFIG_MAX_SCSCF_NUM];
static uint8_t gScscfAddrNum;


osXmlData_t scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM] = {
    {SCSCF_USR_PROFILE_IDENTITY,                     {"Identity", sizeof("Identity")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_PRIVATEID,                    {"PrivateID", sizeof("PrivateID")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_PUBLICIDENTITY,               {"PublicIdentity", sizeof("PublicIdentity")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
    {SCSCF_USR_PROFILE_SHAREDIFCSETID,               {"SharedIFCSetID", sizeof("SharedIFCSetID")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_BARRINGINDICATION,            {"BarringIndication", sizeof("BarringIndication")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
};



void cscfConfig_init(char* cxFolder, char* cxXsdFileName)
{
    osMBuf_t* xsdMBuf = osXsd_initNS(cxFolder, cxXsdFileName);
    if(!xsdMBuf)
    {
        logError("fails to osXsd_initNS from %s/%s for Cx xsd", cxFolder, cxXsdFileName);
        return;
    }

	osPL_str2PLdup(&gCxXsdName, cxXsdFileName, strlen(cxXsdFileName));

debug("to-remove, gCxXsdName=%r.", &gCxXsdName);
	//perform cscf config xml parsing

	//set gScscfSockAddr and gIcscfSockAddr
	cscfConfig_setGlobalSockAddr();
}


osStatus_e scscfConfig_parseUserProfile(osPointerLen_t* pRawUserProfile, scscfUserProfile_t* pDecodedUserProfile)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pRawUserProfile || !pDecodedUserProfile)
	{
		logError("null pointer, pRawUserProfile=%p, pDecodedUserProfile=%p.", pRawUserProfile, pDecodedUserProfile);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	mdebug(LM_CSCF, "Cx xsdName=%r, parse user profile:\n%r", &gCxXsdName, pRawUserProfile);	
	osMBuf_t xmlMBuf = {(uint8_t*)pRawUserProfile->p, pRawUserProfile->l, 0, pRawUserProfile->l};
	pDecodedUserProfile->impuNum = 0;
	pDecodedUserProfile->sIfcIdList.sIfcIdNum = 0;
    osXmlDataCallbackInfo_t cbInfo={true, false, false, scscfConfig_userProfileCB, pDecodedUserProfile, scscfConfig_xmlUsrProfileData, SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM};
	osXml_getElemValue(&gCxXsdName, NULL, &xmlMBuf, true, &cbInfo);

EXIT:
	return status;
}	



static void scscfConfig_userProfileCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData)
{
    if(!pXmlValue)
    {
        logError("null pointer, pXmlValue.");
        return;
    }

	scscfUserProfile_t* pDecodedUserProfile = appData;

	static __thread scscfImpuInfo_t* pImpuInfo = NULL;
    switch(pXmlValue->eDataName)
    {
		case SCSCF_USR_PROFILE_IDENTITY:
			if(!pImpuInfo)
			{
				logError("received a identity(%r), but pImpuInfo has not been created.", &pXmlValue->xmlStr);
				return;
			}

			pImpuInfo->impu = pXmlValue->xmlStr;
			mdebug(LM_CSCF, "dataName=%r, IMPU=%r", &scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_IDENTITY].dataName, &pImpuInfo->impu); 

			break;
		case SCSCF_USR_PROFILE_PRIVATEID:
			pDecodedUserProfile->impi = pXmlValue->xmlStr;
            mdebug(LM_CSCF, "dataName=%r, impi=%r", &scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_PRIVATEID].dataName, &pXmlValue->xmlStr);

			break;
		case SCSCF_USR_PROFILE_PUBLICIDENTITY:
			if(pXmlValue->isEOT)
			{
				pImpuInfo = NULL;
			}
			else
			{
				if(pDecodedUserProfile->impuNum >= SCSCF_MAX_ALLOWED_IMPU_NUM)
				{
    	            logError("the provisioned number of impu exceeds maximum allowed(%d).", SCSCF_MAX_ALLOWED_IMPU_NUM);
        	        return;
	            }

            	pImpuInfo = &pDecodedUserProfile->impuInfo[pDecodedUserProfile->impuNum++];
			}
			mdebug(LM_CSCF, "dataName=%r, isEOT=%d.", &scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_PUBLICIDENTITY].dataName, pXmlValue->isEOT);

            break;
		case SCSCF_USR_PROFILE_SHAREDIFCSETID:
			if(pDecodedUserProfile->sIfcIdList.sIfcIdNum >= SCSCF_MAX_ALLOWED_SIFC_ID_NUM)
			{
				logError("the provisioned number of sIfcId exceeds maximum allowed(%d).", SCSCF_MAX_ALLOWED_SIFC_ID_NUM);
				return;
			}
			pDecodedUserProfile->sIfcIdList.sIfcId[pDecodedUserProfile->sIfcIdList.sIfcIdNum++] = pXmlValue->xmlInt;
			mdebug(LM_CSCF, "dataName=%r, sIfcId=%d.", &scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_SHAREDIFCSETID].dataName, pXmlValue->xmlInt);

			break;
		case SCSCF_USR_PROFILE_BARRINGINDICATION:
			if(!pImpuInfo)
            {
                logError("received a identity(%r), but pImpuInfo has not been created.", &pXmlValue->xmlStr);
                return;
            }

            pImpuInfo->isBarred = pXmlValue->xmlIsTrue;
			mdebug(LM_CSCF, "dataName=%r, isBarred=%d.", &scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_BARRINGINDICATION].dataName, pXmlValue->xmlIsTrue);

			break;
		default:
			logError("received unexpected xml eDataName(%d), ignore.", pXmlValue->eDataName);
			break; 
	}
}


struct sockaddr_in cscfConfig_getLocalSockAddr(cscfType_e cscfType, bool isUseListenPort)
{
	struct sockaddr_in sockAddr = cscfType == CSCF_TYPE_ICSCF ? gIcscfSockAddr : gScscfSockAddr;

	if(!isUseListenPort)
	{
		sockAddr.sin_port = 0;
	}

	return sockAddr;
}


bool cscf_isS(struct sockaddr_in* rcvLocal)
{
	return osIsSameSA(rcvLocal, &gScscfSockAddr) ? true : false;
}


bool icscfConfig_getScscfInfoByCap(uint32_t capValue, sipTuAddr_t* pScscfAddr, bool* isLocal)
{
	for(int i=0; i<gScscfAddrNum; i++)
	{
		if(gScscfAddrInfo[i].capValue == capValue)
		{
			pScscfAddr->isSockAddr = true;
			pScscfAddr->sockAddr = gScscfAddrInfo[i].sockAddr;
			pScscfAddr->tpType = gScscfAddrInfo[i].tpType;
			*isLocal = gScscfAddrInfo[i].isLocal;
			
			return true;
			break;
		}
	}

	return false;
}


scscfAddrInfo_t* icscfConfig_getScscfInfo(uint8_t* pScscfNum)
{
	if(!pScscfNum)
	{
		return NULL;
	}

	return gScscfAddrInfo;
}


bool icscfConfig_getScscfInfoByName(osPointerLen_t* pScscfName, sipTuAddr_t* pScscfAddr, bool* isLocal)
{
	for(int i=0; i<gScscfAddrNum; i++)
    {
		if(osPL_casecmp(pScscfName, &gScscfAddrInfo[i].scscfName) == 0)
        {
            pScscfAddr->isSockAddr = true;
            pScscfAddr->sockAddr = gScscfAddrInfo[i].sockAddr;
            pScscfAddr->tpType = gScscfAddrInfo[i].tpType;
            *isLocal = gScscfAddrInfo[i].isLocal;

            return true;
            break;
        }
    }

    return false;
}


static void cscfConfig_setGlobalSockAddr()
{
	osIpPort_t icscfIpPort = {{{ICSCF_IP_ADDR, strlen(ICSCF_IP_ADDR)}}, ICSCF_LISTEN_PORT};
	osConvertPLton(&icscfIpPort, true, &gIcscfSockAddr);

    osIpPort_t scscfIpPort = {{{SCSCF_IP_ADDR, strlen(SCSCF_IP_ADDR)}}, SCSCF_LISTEN_PORT};
    osConvertPLton(&scscfIpPort, true, &gScscfSockAddr);
}
