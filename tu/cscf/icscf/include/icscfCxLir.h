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


typedef void (*icscfLir4ScscfCB_h)(void* pScscfId);

osStatus_e icscf_performLir4Scscf(osPointerLen_t* pImpu, icscfLir4ScscfCB_h icscfLir4ScscfCb, void* scscfId);


#endif
