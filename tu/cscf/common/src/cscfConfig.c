/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file cscfConfig.c
 ********************************************************/

#include <string.h>
#include <stdio.h>

#include "osPL.h"
#include "osMBuf.h"
#include "osSockAddr.h"
#include "osMemory.h"

#include "sipTU.h"
#include "sipHdrMisc.h"

#include "cscfConfig.h"
#include "scscfRegistrar.h"



static void scscfConfig_userProfileCB(osXmlData_t* pXmlValue, void* nsInfo, void* appData);
static void cscfConfig_setGlobalSockAddr();


static osPointerLen_t gCxXsdName;
static struct sockaddr_in gScscfSockAddr, gIcscfSockAddr;
static scscfAddrInfo_t gScscfAddrInfo[ICSCF_CONFIG_MAX_SCSCF_NUM];
static uint8_t gScscfAddrNum;
static sipTuAddr_t gScscfLocalAddr;	//the local SCSCF addr
static sipTuAddr_t gIcscfLocalAddr; //the local ICSCF addr


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

	gScscfLocalAddr.isSockAddr = false;
	gScscfLocalAddr.tpType = TRANSPORT_TYPE_ANY;
	gScscfLocalAddr.ipPort.ip.p = SCSCF_IP_ADDR;
	gScscfLocalAddr.ipPort.ip.l = strlen(SCSCF_IP_ADDR);
	gScscfLocalAddr.ipPort.port = SCSCF_LISTEN_PORT;

    gIcscfLocalAddr.isSockAddr = false;
    gIcscfLocalAddr.tpType = TRANSPORT_TYPE_ANY;
    gIcscfLocalAddr.ipPort.ip.p = SCSCF_IP_ADDR;
    gIcscfLocalAddr.ipPort.ip.l = strlen(ICSCF_IP_ADDR);
    gIcscfLocalAddr.ipPort.port = ICSCF_LISTEN_PORT;
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

	osMBuf_writePL(&pDecodedUserProfile->rawUserProfile, pRawUserProfile, false);
//	osMBuf_t xmlMBuf = {(uint8_t*)pRawUserProfile->p, pRawUserProfile->l, 0, pRawUserProfile->l};
	pDecodedUserProfile->impuNum = 0;
	pDecodedUserProfile->sIfcIdList.sIfcIdNum = 0;
    osXmlDataCallbackInfo_t cbInfo={true, false, false, scscfConfig_userProfileCB, pDecodedUserProfile, scscfConfig_xmlUsrProfileData, SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM};
	osXml_getElemValue(&gCxXsdName, NULL, &pDecodedUserProfile->rawUserProfile, true, &cbInfo);

	//reset rawUserProfile pos to the beginning
	osMBuf_setPos(&pDecodedUserProfile->rawUserProfile, 0);
	scscf_dbgListUsrProfile(pDecodedUserProfile);

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


//if the rcvLocal a SCSCF address
bool cscf_isS(struct sockaddr_in* rcvLocal)
{
	return osIsSameSA(rcvLocal, &gScscfSockAddr) ? true : false;
}


sipTuAddr_t* cscfConfig_getOwnAddr(cscfType_e cscfType)
{
	sipTuAddr_t* pAddr = NULL;

	switch(cscfType)
	{
		case CSCF_TYPE_ICSCF:
			pAddr = &gIcscfLocalAddr;
			break;
		case CSCF_TYPE_SCSCF:
			pAddr = &gScscfLocalAddr;
			break;
		default:
			logError("unexpected cscfType(%d).", cscfType);
			break;
	}

	return pAddr;
}


void cscfConfig_getMgcpAddr(sipTuAddr_t* pMgcpAddr)
{
	pMgcpAddr->isSockAddr = false;
	pMgcpAddr->tpType = TRANSPORT_TYPE_ANY;
	osPL_setStr1(&pMgcpAddr->ipPort.ip, MGCP_IP_ADDR, strlen(MGCP_IP_ADDR));
	pMgcpAddr->ipPort.port = MGCP_LISTEN_PORT;
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


bool cscfConfig_getScscfInfoByName(osPointerLen_t* pScscfName, sipTuAddr_t* pScscfAddr, bool* isLocal)
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


/* simply compare with the various configured own scscf in order.  topCheck is introduced so that if a scscf match was found, 
 * the enxt check will check the same scscf pattern first, with the hope that the configured own scscf would not change often
 * 
 * there shall be no WSP between uri and port, between sip: and uri.
 */
bool cscfConfig_isOwnScscf(osPointerLen_t* pScscfName)
{
	static int topCheck = 1;

	if(!pScscfName)
	{
		logError("null pointer, pScscfName.");
		return false;
	}

	int topChecked = 0;
	switch(topCheck)
	{
		case 1:
		    if(pScscfName->l == strlen(SCSCF_URI_WITH_PORT) || osPL_strcasecmp(pScscfName, SCSCF_URI_WITH_PORT) == 0)
    		{
				return true;
			}
        		
			topChecked = 1;
			break;	
		case 2:
		    if(pScscfName->l == strlen(SCSCF_URI) && osPL_strcasecmp(pScscfName, SCSCF_URI) == 0)
    		{
        		return true;
    		}

			topChecked = 2;
			break;
		case 3:
		    if(pScscfName->l == strlen(SCSCF_IP_WITH_PORT) && osPL_strcasecmp(pScscfName, SCSCF_IP_WITH_PORT) == 0)
    		{
        		return true;
    		}

			topChecked = 3;
			break;
		case 4:
    		if(pScscfName->l == strlen(SCSCF_IP_ADDR) && osPL_strcasecmp(pScscfName, SCSCF_IP_ADDR) == 0)
    		{
        		return true;
    		}

			topChecked = 4;
			break;
		default:
			logError("unexpected topCheck(%d).", topCheck);
			break;
	}

	if(topChecked != 1 && pScscfName->l == strlen(SCSCF_URI_WITH_PORT) && osPL_strcasecmp(pScscfName, SCSCF_URI_WITH_PORT) == 0)
	{
		topCheck = 1;
		return true;
	}

	if(topChecked != 2 && pScscfName->l == strlen(SCSCF_URI) && osPL_strcasecmp(pScscfName, SCSCF_URI) == 0)
	{
		topCheck = 2;
		return true;
	}

	if(topChecked != 3 && pScscfName->l == strlen(SCSCF_IP_WITH_PORT) && osPL_strcasecmp(pScscfName, SCSCF_IP_WITH_PORT) == 0)
	{
		topCheck = 3;
		return true;
	}

	if(topChecked != 4 && pScscfName->l == strlen(SCSCF_IP_ADDR) && osPL_strcasecmp(pScscfName, SCSCF_IP_ADDR) == 0)
    {
		topCheck = 4;
        return true;
    }

	return false;
}
		

void scscf_dbgListUsrProfile(scscfUserProfile_t* pUsrProfile)
{
	if(!pUsrProfile)
	{
		return;
	}

	
	mdebug(LM_CSCF, "HSS provided user profile:\nimpi=%r", &pUsrProfile->impi);
	for(int i=0; i<pUsrProfile->impuNum; i++)
	{
		mdebug1(LM_CSCF, "impu[%d]=%r, isbarred=%d\n", i, &pUsrProfile->impuInfo[i].impu, pUsrProfile->impuInfo[i].isBarred);
	}
	mdebug1(LM_CSCF, "sIfcId=");
	for(int i=0; i<pUsrProfile->sIfcIdList.sIfcIdNum; i++)
	{
		mdebug1(LM_CSCF, "%d,  ", pUsrProfile->sIfcIdList.sIfcId[i]);
	}
	mdebug1(LM_CSCF, "\n");
}


static void cscfConfig_setGlobalSockAddr()
{
	osIpPort_t icscfIpPort = {{{ICSCF_IP_ADDR, strlen(ICSCF_IP_ADDR)}}, ICSCF_LISTEN_PORT};
	osConvertPLton(&icscfIpPort, true, &gIcscfSockAddr);

    osIpPort_t scscfIpPort = {{{SCSCF_IP_ADDR, strlen(SCSCF_IP_ADDR)}}, SCSCF_LISTEN_PORT};
    osConvertPLton(&scscfIpPort, true, &gScscfSockAddr);
}
