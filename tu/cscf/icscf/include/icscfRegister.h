/********************************************************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file icscfRegistrar.h
 * implement 3GPP 24.229 ICSCF registration header file
 ********************************************************************************************/


#ifndef __ICSCF_REGISTER_H
#define _ICSCF_REGISTER_H


#include "osList.h"
#include "osPL.h"

#include "diaMsg.h"
#include "diaCxUar.h"
#include "diaCxAvp.h"



typedef struct {
    DiaCxUarAuthType_e authType;
    diaResultCode_t rspCode;
    bool isCap;     //=true, use serverCap, otherwise, use serverName
    bool isCapExist;    //if UAA has CAP avp.  note it is possible that isCap=true & isCapExist=false, meaning the UAA does not have server name neither CAP avp
    osListPlusElement_t curLE; //the LE that stores the current cap in serverCap, only valid when isCap = true and isCapExist = true
    osPointerLen_t serverName;
    diaCxServerCap_t serverCap; //for now, only support using cap value, not support using server name inside this avp
    diaMsgDecoded_t* pMsgDecoded;
} icscfUaaInfo_t;


typedef struct {
    osPointerLen_t impi;
    osPointerLen_t impu;
    sipTUMsg_t* pSipTUMsg;
    sipMsgDecodedRawHdr_t* pReqDecodedRaw;
    icscfUaaInfo_t uaaInfo;
} icscfRegInfo_t;



osStatus_e icscfCx_performUar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, DiaCxUarAuthType_e authType, icscfRegInfo_t* pRegInfo);
osStatus_e icscfReg_decodeUaa(diaMsgDecoded_t* pDiaDecoded, icscfUaaInfo_t* pUaaInfo);
void icscfReg_onDiaMsg(diaMsgDecoded_t* pDiaDecoded, void* pAppData);

#endif
