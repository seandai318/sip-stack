/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file icscfLir.c
 * implement 3GPP 28.229 ICSCF Cx LIR functions
 ********************************************************/


#include "osHash.h"
#include "osTimer.h"
#include "osSockAddr.h"

#include "diaMsg.h"
#include "diaIntf.h"
#include "diaCxSar.h"
#include "diaCxAvp.h"

#include "cscfConfig.h"
#include "cscfHelper.h"
#include "icscfRegister.h"
#include "icscfCxLir.h"



osStatus_e icscf_performLir4Scscf(osPointerLen_t* pImpu, icscfLir4ScscfCB_h icscfLir4ScscfCb, void* scscfId)
{
	osStatus_e status = OS_STATUS_OK;

EXIT:
	return status;
}
