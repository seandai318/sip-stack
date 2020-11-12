/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipConfig.h
 ********************************************************/

#ifndef _CSCF_CONFIG_H
#define _CSCF_CONFIG_H

#include "osPL.h"

#include "scscfRegistrar.h"


#define SCSCF_MAX_ALLOWED_SIFC_ID_NUM	20	
#define SCSCF_MAX_ALLOWED_IMPU_ID_NUM   10


#define SCSCFREG_USER_PURGE_TIME		36000		//sec, user purge time for no activity, 10 hour

#define SCSCF_URI
#define SCSCF_URI_WITH_PORT

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


void cscfConfig_init(char* cxFolder, char* cxXsdFileName);
osStatus_e scscfConfig_parseUserProfile(osPointerLen_t* pRawUserProfile, scscfReg_userProfile_t* pDecodedUserProfile);



#endif
