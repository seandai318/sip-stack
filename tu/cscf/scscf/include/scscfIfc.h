#ifndef __SCSCF_IFC_H
#define __SCSCF_IFC_H


#include "osPL.h"
#include "osList.h"

#include "sipMsgFirstLine.h"
#include "sipHeaderData.h"
#include "sipMsgRequest.h"

#include "scscfRegistrar.h"


typedef enum {
    SCSCF_IFC_SESS_CASE_INVALID = -1,
    SCSCF_IFC_SESS_CASE_ORIGINATING = 0,        //"Originating", Originating
    SCSCF_IFC_SESS_CASE_TERM_REGISTERED = 1,    //"Terminating_Registered", Terminating for a registered end user
    SCSCF_IFC_SESS_CASE_TERM_UNREGISTERED = 2,  //"Terminating_Unregistered", Terminating for an unregistered end user
    SCSCF_IFC_SESS_CASE_ORIG_UNREGISTERED = 3,  //"Originating_Unregistered", Originating for an unregistered end user
    SCSCF_IFC_SESS_CASE_ORIG_CDIV = 4,          //"Originating_CDIV", Originating after Call Diversion services
} scscfIfcSessCase_e;


typedef enum {
	SCSCF_IFC_REG_TYPE_INITIAL_REG,
	SCSCF_IFC_REG_TYPE_RE_REG,
	SCSCF_IFC_REG_TYPE_DE_REG,
} scscfIfcRegType_e;


typedef struct {
	bool isLastAsOK;	//if SCSCF successfully sent SIP request to the last AS, for default handling
	sipRequest_e sipMethod;
	scscfIfcSessCase_e sessCase;
	scscfIfcRegType_e regType;
} scscfIfcEvent_t;


osStatus_e scscfIfc_parseSIfcSet(osPointerLen_t* pSIfc, osList_t* pSIfcSet);
osPointerLen_t* scscfIfc_getNextAS(osListElement_t** ppLastifc, sIfcIdList_t* pSIfcIdList, sipMsgDecodedRawHdr_t* pReqDecodedRaw, scscfIfcEvent_t* pIfcEvent);
osStatus_e scscfIfc_parseSIfcSet(osPointerLen_t* pSIfc, osList_t* pSIfcSet);
scscfIfcRegType_e scscfIfc_mapSar2IfcRegType(scscfRegSarRegType_e sarRegType);

#endif
