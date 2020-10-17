/********************************************************
 * Copyright (C) 2019,2020, Sean Dai
 *
 * @file sipTUConfig.h
 ********************************************************/

#ifndef _SIP_TU_CONFIG_H
#define _SIP_TU_CONFIG_H

#include "sipTUIntf.h"


typedef enum {
	SIPTU_PRODUCT_TYPE_PROXY,
	SIPTU_PRODUCT_TYPE_PROXY_AND_REG,
	SIPTU_PRODUCT_TYPE_PROXY_AND_MAS,
	SIPTU_PRODUCT_TYPE_MAS_AND_REG,
	SIPTU_PRODUCT_TYPE_PROXY_AND_MAS_AND_REG,
} sipTuProductType_e;


#define SIP_TU_PRODUCT_TYPE		SIPTU_PRODUCT_TYPE_PROXY_AND_MAS_AND_REG


#endif

