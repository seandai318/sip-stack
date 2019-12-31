#ifndef _SIP_GENERIC_NAME_PARAM_H
#define _SIP_GENERIC_NAME_PARAM_H

#include "osTypes.h"
#include "osMBuf.h"
#include "osPL.h"
#include "osList.h"
#include "sipUri.h"
#include "sipParamNameValue.h"
#include "sipHdrTypes.h"


#define sipHdrParamNameValue_t sipParamNameValue_t



//this is essentially sipHdrGenericNameParam_t, except that pointer is used for uri so that no need to allocate memory
//when this data structure is used, the uri has been allocated by other function, this data structure just points to it
typedef struct sipHdr_genericNameParamPt {
    osPointerLen_t displayName;
    sipUri_t* pUri;
    osList_t genericParam;      //for both decode and encode, each element is sipParamNameValue_t
} sipHdrGenericNameParamPt_t;


typedef struct sipHdr_replyto {
	sipHdrGenericNameParam_t replyto;
} sipHdrReplyto_t;


//osList_t* pNameParamList contains a list of sipHdrGenericNameParam_t
typedef osStatus_e (*sipParserHdr_multiNameParam_h) (osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddrOnly, sipHdrMultiGenericNameParam_t* pNameParamList);
typedef osStatus_e (*sipParserHdr_genericNameParam_h) (osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddr, sipHdrGenericNameParam_t* pNameParam);

//isNameaddr=1, the expected format is [ display-name ] < SIP-URI / SIPS-URI / absoluteURI >
//isNameaddr=0, the expected format is SIP-URI / SIPS-URI / absoluteURI 
osStatus_e sipParserHdr_genericNameParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddrOnly, sipHdrGenericNameParam_t* pNameParam);
//this function parses *(;name=value)
osStatus_e sipParserHdr_genericParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pStatus);
//this function parses *(,name=value)
osStatus_e sipParserHdr_commaGenericParam(osMBuf_t* pSipMsg, size_t hdrEndPos, sipParsingInfo_t* pParentParsingInfo, sipParsingStatus_t* pStatus);
osStatus_e sipParserHdr_multiNameParam(osMBuf_t* pSipMsg, size_t hdrEndPos, bool isNameaddrOnly, sipHdrMultiGenericNameParam_t* pNameParamList);
osStatus_e sipHdrGenericNameParam_encode(osMBuf_t* pSipBuf, void* pHdrDT, void* pData);
osStatus_e sipParserHdr_replyto(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrReplyto_t* pNameParam);
void sipHdrGenericNameParam_cleanup(void* data);
void sipHdrGenericNameParamDecoded_cleanup(void* data);
void* sipHdrGenericNameParam_alloc();
void* sipHdrMultiGenericNameParam_alloc();

osPointerLen_t* sipHdrGenericNameParam_getGPValue(sipHdrGenericNameParam_t* pHdr, osPointerLen_t* pGPName);

osStatus_e sipHdrGenericNameParam_build(sipHdrGenericNameParamPt_t* pHdr, sipUri_t* pUri, osPointerLen_t* displayname);
osStatus_e sipHdrGenericNameParam_addParam(sipHdrGenericNameParamPt_t* pHdr, sipParamNameValue_t* pNameValue);

#endif
