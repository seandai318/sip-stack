/********************************************************************************************
 * Copyright (C) 2019-2021, Sean Dai
 *
 * @file icscfCxLir.h
 * implement 3GPP 24.229 ICSCF Cx LIR function header file
 ********************************************************************************************/


#ifndef __ICSCF_CX_LIR_H
#define __ICSCF_CX_LIR_H

#include "osTypes.h"
#include "osPL.h"


#define ICSCF_MAX_SCSCF_CAP_NUM	8	//the maximum scscf capabilities that ICSCF will handle (each for mandatory or optional)


typedef struct {
	uint8_t manCapNum;
	uint8_t optCapNum;
	uint32_t mandatoryCap[ICSCF_MAX_SCSCF_CAP_NUM];
	uint32_t optionalCap[ICSCF_MAX_SCSCF_CAP_NUM];
} scscfCapInfo_t;


osStatus_e icscf_performLir4Scscf(osPointerLen_t* pImpu, diaNotifyApp_h icscfLir4ScscfCb, void* scscfId);
osStatus_e icscf_decodeLia(diaMsgDecoded_t* pDiaDecoded, osPointerLen_t* pServerName, scscfCapInfo_t* pCapList, diaResultCode_t* pResultCode);


#endif
