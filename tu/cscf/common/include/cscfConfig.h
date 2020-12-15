/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipConfig.h
 ********************************************************/

#ifndef _CSCF_CONFIG_H
#define _CSCF_CONFIG_H


#include <netinet/in.h>

#include "osPL.h"
#include "osXmlParserIntf.h"

#include "sipTU.h"

#define SCSCF_MAX_ALLOWED_SIFC_ID_NUM	20	
#define SCSCF_MAX_ALLOWED_IMPU_NUM   	10


#define SCSCF_REG_DEFAULT_EXPIRE		7200		//sec
#define SCSCF_REG_MIN_EXPIRE			600			//sec
#define SCSCF_REG_MAX_EXPIRE			36000		//sec
#define SCSCF_REG_USER_PURGE_TIME		36000		//sec, user purge time for no activity, 10 hour

#define SCSCF_IS_AUTH_ENABLED			false
#define SCSCF_IS_REREG_PERFORM_AUTH		false

#define SCSCF_URI						"scscf01.ims.com"
#define SCSCF_URI_WITH_PORT				"scscf01.ims.com:5060"
#define SCSCF_IP_ADDR					"1.2.3.4"
#define SCSCF_LISTEN_PORT				5060

#define SCSCF_SIFC_XML_FILE_NAME			"sifc.xml"
#define SCSCF_SIFC_XSD_FILE_NAME            "sifc.xsd"

#define CSCF_CONFIG_FOLDER              "/project/app/mas/config"
#define CSCF_HSS_URI				"hss.ims.com"

#define SCSCF_HASH_SIZE				1024


#define ICSCF_IP_ADDR                   "2.3.4.5"
#define ICSCF_LISTEN_PORT               5060
#define ICSCF_HASH_SIZE				1024
#define ICSCF_CONFIG_MAX_SCSCF_NUM	6
#define ICSCF_UAR_AUTHTYPE_CAPABILITY	true

typedef enum {
	CSCF_TYPE_INVALID,
	CSCF_TYPE_ICSCF,
	CSCF_TYPE_SCSCF,
} cscfType_e;


typedef enum {
    SCSCF_USR_PROFILE_IDENTITY,             //Identity
    SCSCF_USR_PROFILE_PRIVATEID,            //PrivateID
    SCSCF_USR_PROFILE_PUBLICIDENTITY,       //PublicIdentity
    SCSCF_USR_PROFILE_SHAREDIFCSETID,       //SharedIFCSetID
    SCSCF_USR_PROFILE_BARRINGINDICATION,    //BarringIndication
    SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM,
} scscfUserProfileParam_e;



osXmlData_t scscfConfig_xmlUsrProfileData[SCSCF_USR_PROFILE_MAX_DATA_NAME_NUM] = {
    {SCSCF_USR_PROFILE_IDENTITY,                     {"Identity", sizeof("Identity")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_PRIVATEID,                    {"PrivateID", sizeof("PrivateID")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_PUBLICIDENTITY,               {"PublicIdentity", sizeof("PublicIdentity")-1}, OS_XML_DATA_TYPE_COMPLEX, true},
    {SCSCF_USR_PROFILE_SHAREDIFCSETID,               {"SharedIFCSetID", sizeof("SharedIFCSetID")-1}, OS_XML_DATA_TYPE_SIMPLE, false},
    {SCSCF_USR_PROFILE_BARRINGINDICATION,            {"SubscriptionId", sizeof("SubscriptionId")-1}, OS_XML_DATA_TYPE_ANY, false},
};


typedef struct {
    osPointerLen_t impu;
    bool isBarred;
} scscfImpuInfo_t;


typedef struct {
    uint32_t sIfcId[SCSCF_MAX_ALLOWED_SIFC_ID_NUM];
    uint32_t sIfcIdNum;
} sIfcIdList_t;


typedef struct scscfUserProfile {
    osPointerLen_t impi;
	scscfImpuInfo_t impuInfo[SCSCF_MAX_ALLOWED_IMPU_NUM];
	uint8_t impuNum;
    sIfcIdList_t sIfcIdList;
//  osList_t sIfcId;            //each entry contains a int, sorted from small to bigger
} scscfUserProfile_t;


typedef struct {
    uint32_t capValue;
    transportType_e tpType;
    bool isLocal;
    struct sockaddr_in sockAddr;
    osPointerLen_t scscfName;
} scscfAddrInfo_t;


void cscfConfig_init(char* cxFolder, char* cxXsdFileName);
osStatus_e scscfConfig_parseUserProfile(osPointerLen_t* pRawUserProfile, scscfUserProfile_t* pDecodedUserProfile);
struct sockaddr_in cscfConfig_getLocalSockAddr(cscfType_e cscfType, bool isUseListenPort);
bool cscf_isS(struct sockaddr_in* rcvLocal);

bool icscfConfig_getScscfInfoByCap(uint32_t capValue, sipTuAddr_t* pScscfAddr, bool* isLocal);
bool icscfConfig_getScscfInfoByName(osPointerLen_t* pScscfName, sipTuAddr_t* pScscfAddr, bool* isLocal);


#endif
