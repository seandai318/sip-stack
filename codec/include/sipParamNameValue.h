#ifndef _SIP_PARAM_NAME_VALUE_H
#define _SIP_PARAM_NAME_VALUE_H


#include "osPL.h"


typedef struct sipHdrParamNameValue {
    osPointerLen_t name;
    osPointerLen_t value;
} sipParamNameValue_t;


#define sipParamName_t osPointerLen_t


#endif
