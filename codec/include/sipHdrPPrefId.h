/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipHdrPPrefId.h
 ********************************************************/

#ifndef _SIP_HDR_PPREFID_H
#define _SIP_HDR_PPREFID_H

#include "osList.h"
#include "sipGenericNameParam.h"
#include "sipHdrTypes.h"

#define sipHdrPpi_t sipHdrMultiGenericNameParam_t

#define sipHdrPpi_cleanup sipHdrMultiGenericNameParam_cleanup

osStatus_e sipParserHdr_ppi(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrPpi_t* pPpi);


#endif
