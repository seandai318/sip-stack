#ifndef __SCSCF_CX_H
#define __SCSCF_CX_H


#include "osPL.h"
#include "osTypes.h"

#include "diaMsg.h"
#include "diaCxSar.h"

#include "scscfRegistrar.h"


osStatus_e scscfReg_decodeHssMsg(diaMsgDecoded_t* pDiaDecoded, scscfRegInfo_t* pRegInfo, diaResultCode_t* pResultCode);
osStatus_e scscfReg_performMar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, scscfRegInfo_t* pRegInfo);
osStatus_e scscfReg_performSar(osPointerLen_t* pImpi, osPointerLen_t* pImpu, scscfRegInfo_t* pRegData, dia3gppServerAssignmentType_e saType, int regExpire);
osStatus_e scscfReg_onSaa(scscfRegInfo_t* pRegInfo, diaResultCode_t resultCode);
osStatus_e scscfReg_onMaa(scscfRegInfo_t* pRegInfo, diaResultCode_t resultCode);

osStatus_e scscfReg_decodeSaa(diaMsgDecoded_t* pDiaDecoded, scscfUserProfile_t* pUsrProfile, diaResultCode_t* pResultCode, scscfChgInfo_t* pChgInfo);


#endif
