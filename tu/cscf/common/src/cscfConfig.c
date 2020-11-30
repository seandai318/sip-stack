/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file cscfConfig.c
 ********************************************************/

#include "osPL.h"
#include "osMBuf.h"
#include "osSockAddr.h"

#include "cscfConfig.h"



static void scscfConfig_userProfileCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData);

static osPointerLen_t cxXsdName;


void cscfConfig_init(char* cxFolder, char* cxXsdFileName)
{
    osMBuf_t* xsdMBuf = osXsd_initNS(cxFolder, cxXsdFileName);
    if(!xsdMBuf)
    {
        logError("fails to osXsd_initNS from %s/%s for Cx xsd", cxFolder, cxXsdFileName);
        return;
    }

	char* cxXsdNamePtr = osmalloc(strlen(cxXsdFileName), NULL);
	osPL_setStr(&cxXsdName, cxXsdNamePtr, strlen(cxXsdFileName));
}


osStatus_e scscfConfig_parseUserProfile(osPointerLen_t* pRawUserProfile, scscfReg_userProfile_t* pDecodedUserProfile)
{
	osMBuf_t* pXmlBuf = osMBuf_setPL(pRawUserProfile);
    osXmlDataCallbackInfo_t cbInfo={true, false, false, scscfConfig_userProfileCB, pDecodedUserProfile, scscfConfig_xmlUsrProfileData, SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM};
	osXml_getElemValue(&cxXsdName, NULL, xmlMBuf, true, &cbInfo);
	osMBuf_freeHdr(pXmlBuf);
}	



static void scscfConfig_userProfileCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData)
{
    if(!pXmlValue)
    {
        logError("null pointer, pXmlValue.");
        return;
    }

	scscfReg_userProfile_t* pDecodedUserProfile = appData;

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
			break;
		case SCSCF_USR_PROFILE_PRIVATEID:
			pDecodedUserProfile.impi = pXmlValue->xmlStr;
			break;
		case SCSCF_USR_PROFILE_PUBLICIDENTITY:
			if(pXmlValue->isEOT)
			{
				pImpuInfo = NULL;
			}
			else
			{
				if(pDecodedUserProfile->impuNum >= SCSCF_MAX_ALLOWED_IMPU_ID_NUM)
				{
    	            logError("the provisioned number of impu exceeds maximum allowed(%d).", SCSCF_MAX_ALLOWED_IMPU_NUM);
        	        return;
	            }

            	pImpuInfo = &pDecodedUserProfile->impuInfo[pDecodedUserProfile->impuNum++];
			}
            break;
		case SCSCF_USR_PROFILE_SHAREDIFCSETID:
			if(pDecodedUserProfile->sIfcIdNum >= SCSCF_MAX_ALLOWED_SIFC_ID_NUM)
			{
				logError("the provisioned number of sIfcId exceeds maximum allowed(%d).", SCSCF_MAX_ALLOWED_SIFC_ID_NUM);
				return;
			}
			pDecodedUserProfile->sIfcId[pDecodedUserProfile->sIfcIdNum++] = pXmlValue->xmlInt;
			break;
		case SCSCF_USR_PROFILE_BARRINGINDICATION:
			if(!pImpuInfo)
            {
                logError("received a identity(%r), but pImpuInfo has not been created.", &pXmlValue->xmlStr);
                return;
            }

            pImpuInfo->isBarred = pXmlValue->xmlIsTrue;
			break;
		default:
			logError("received unexpected xml eDataName(%d), ignore.", pXmlValue->eDataName);
			break; 
	}
}


struct sockaddr_in cscfConfig_getLocalSockAddr(cscfType_e cscfType, bool isUseListenPort)
{
	struct sockaddr_in sockAddr = {};

	switch(cscfType)
	{
		case CSCF_TYPE_ICSCF:
		{
			osIpPort_t ipPort = {{{ICSCF_IP_ADDR, strlen(ICSCF_IP_ADDR)}}, isUseListenPort ? ICSCF_LISTEN_PORT : 0};
			osConvertPLton(&ipPort, true, &sockAddr);
			break;
		}
		case CSCF_TYPE_SCSCF:
        {
            osIpPort_t ipPort = {{{SCSCF_IP_ADDR, strlen(SCSCF_IP_ADDR)}}, isUseListenPort ? SCSCF_LISTEN_PORT : 0};
            osConvertPLton(&ipPort, true, &sockAddr);
            break;
        }
		default:
			logError("unexpected cscfType(%d).", cscfType);
			break;
		}
	}

	return sockAddr;
}	
