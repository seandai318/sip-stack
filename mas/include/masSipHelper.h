#ifndef _MAS_SIP_HELPER_H
#define _MAS_SIP_HELPER_H


osMBuf_t* masSip_buildRequest(osPointerLen_t* user, osPointerLen_t* caller, sipUri_t* pCalledContactUser, osPointerLen_t* sms, sipTransViaInfo_t* pTransViaId, size_t* pViaProtocolPos);
osStatus_e masSip_buildContent(osPointerLen_t* sms, osPointerLen_t* caller, osPointerLen_t* called, bool isNewline, osDPointerLen_t* content);
osStatus_e masSip_getSms(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pSms);


#endif
