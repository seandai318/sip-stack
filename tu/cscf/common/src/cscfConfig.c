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
static void cscfConfig_setGlobalSockAddr();


static osPointerLen_t cxXsdName;
static struct sockaddr_in gScscfSockAddr, gIcscfSockAddr;


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

	//perform cscf config xml parsing

	//set gScscfSockAddr and gIcscfSockAddr
	cscfConfig_setGlobalSockAddr();
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


static void cscfConfig_setGlobalSockAddr()
{
	osIpPort_t ipPort = {{{ICSCF_IP_ADDR, strlen(ICSCF_IP_ADDR)}}, ICSCF_LISTEN_PORT};
	osConvertPLton(&ipPort, true, &gIcscfSockAddr);

    osIpPort_t ipPort = {{{SCSCF_IP_ADDR, strlen(SCSCF_IP_ADDR)}}, SCSCF_LISTEN_PORT};
    osConvertPLton(&ipPort, true, &gScscfSockAddr);
}
