#ifndef _SIP_HEADER_PRIV_H
#define _SIP_HEADER_PRIV_H

#include "osTypes.h"
#include "sipHeader.h"
#include "sipHdrAcceptedContact.h"
#include "sipHdrDate.h"
#include "sipHdrMisc.h"
#include "sipHdrPPrefId.h"
#include "sipHdrContact.h"
#include "sipHdrFromto.h"
#include "sipHdrNameaddrAddrspec.h"
#include "sipHdrRoute.h"
#include "sipHdrContentType.h"
#include "sipHdrGperfHash.h"
#include "sipHdrPani.h"
#include "sipHdrVia.h"
#include "sipHdrNameValue.h"


typedef struct sipHdrCreateEncode {
	sipHdrName_e hdrCode;
//	sipHdrCreate_h createHandler;
	sipHdrEncode_h encodeHandler; 
	bool isMandatory;
	bool isPriority;	//the hdr shall be put in the beginning or at the end
	bool isAllowMultiHdr;
} sipHdrCreateEncode_t;


static sipHdrCreateEncode_t sipHdrCreateArray[SIP_HDR_COUNT]={
    {SIP_HDR_NONE, NULL, false, false, false},
    {SIP_HDR_ACCEPT, NULL, false, false, true},
    {SIP_HDR_ACCEPT_CONTACT, sipHdrNameValue_encode, false, false, true},
    {SIP_HDR_ACCEPT_ENCODING, NULL, false, false, true},
    {SIP_HDR_ACCEPT_LANGUAGE, NULL, false, false, true},
    {SIP_HDR_ACCEPT_RESOURCE_PRIORITY, NULL, false, false, true},
    {SIP_HDR_ALERT_INFO, NULL, false, false, true},
    {SIP_HDR_ALLOW, sipHdrName_encode, false, false, true},
    {SIP_HDR_ALLOW_EVENTS, sipHdrName_encode, false, false, true},
    {SIP_HDR_ANSWER_MODE, NULL, false, false, false},
    {SIP_HDR_AUTHENTICATION_INFO, NULL, false, false, true},
    {SIP_HDR_AUTHORIZATION, NULL, false, false, false},
    {SIP_HDR_CALL_ID, sipHdrPL_encode, true, true, false},
    {SIP_HDR_CALL_INFO, NULL, false, false, true},
    {SIP_HDR_CONTACT, sipHdrGenericNameParam_encode, false, false, true},
    {SIP_HDR_CONTENT_DISPOSITION, NULL, false, false, false},
    {SIP_HDR_CONTENT_ENCODING, NULL, false, false, true},
    {SIP_HDR_CONTENT_LANGUAGE, NULL, false, false, true},
    {SIP_HDR_CONTENT_LENGTH, sipHdrLenTime_encode, false, false, false},
    {SIP_HDR_CONTENT_TYPE, NULL, false, false, false},
    {SIP_HDR_CONTRIBUTION_ID, NULL, false, false, false},
    {SIP_HDR_CONVERSATION_ID, NULL, false, false, false},
    {SIP_HDR_CSEQ, sipHdrCSeq_encode, true, false, false},
    {SIP_HDR_DATE, sipHdrName_encode, false, false, false},
    {SIP_HDR_ENCRYPTION, NULL, false, false, false},
    {SIP_HDR_ERROR_INFO, NULL, false, false, true},
    {SIP_HDR_EVENT, NULL, false, false, true},
    {SIP_HDR_EXPIRES, sipHdrLenTime_encode, false, false, false},
    {SIP_HDR_FLOW_TIMER, NULL, false, false, false},
    {SIP_HDR_FROM, sipHdrFrom_encode, true, true, false},
    {SIP_HDR_HIDE, NULL, false, false, false},
    {SIP_HDR_HISTORY_INFO, NULL, false, false, true},
    {SIP_HDR_IDENTITY, NULL, false, false, false},
    {SIP_HDR_IDENTITY_INFO, NULL, false, false, false},
    {SIP_HDR_IN_REPLY_TO, NULL, false, false, true},
    {SIP_HDR_JOIN, NULL, false, false, false},
    {SIP_HDR_MAX_BREADTH, NULL, false, false, false},
    {SIP_HDR_MAX_FORWARDS, sipHdrLenTime_encode, true, true, false},
    {SIP_HDR_MIME_VERSION, NULL, false, false, false},
    {SIP_HDR_MIN_EXPIRES, sipHdrLenTime_encode, false, false, false},
    {SIP_HDR_MIN_SE, NULL, false, false, false},
    {SIP_HDR_ORGANIZATION, NULL, false, false, false},
    {SIP_HDR_P_ACCESS_NETWORK_INFO, sipHdrNameValue_encode, false, false, false},
    {SIP_HDR_P_ANSWER_STATE, NULL, false, false, false},
    {SIP_HDR_P_ASSERTED_IDENTITY, NULL, false, false, true},
    {SIP_HDR_P_ASSOCIATED_URI, NULL, false, false, true},
    {SIP_HDR_P_CALLED_PARTY_ID, NULL, false, false, false},
    {SIP_HDR_P_CHARGING_FUNCTION_ADDRESSES, NULL, false, false, true},
    {SIP_HDR_P_CHARGING_VECTOR, NULL, false, false, false},
    {SIP_HDR_P_DCS_TRACE_PARTY_ID, NULL, false, false, false},
    {SIP_HDR_P_DCS_OSPS, NULL, false, false, false},
    {SIP_HDR_P_DCS_BILLING_INFO, NULL, false, false, false},
    {SIP_HDR_P_DCS_LAES, NULL, false, false, false},
    {SIP_HDR_P_DCS_REDIRECT, NULL, false, false, false},
    {SIP_HDR_P_EARLY_MEDIA, NULL, false, false, false},
    {SIP_HDR_P_MEDIA_AUTHORIZATION, NULL, false, false, true},
    {SIP_HDR_P_PREFERRED_IDENTITY, NULL, false, false, true},
    {SIP_HDR_P_PROFILE_KEY, NULL, false, false, false},
    {SIP_HDR_P_REFUSED_URI_LIST, NULL, false, false, true},
    {SIP_HDR_P_SERVED_USER, NULL, false, false, false},
    {SIP_HDR_P_USER_DATABASE, NULL, false, false, false},
    {SIP_HDR_P_VISITED_NETWORK_ID, NULL, false, false, true},
    {SIP_HDR_PATH, NULL, false, false, true},
    {SIP_HDR_PERMISSION_MISSING, NULL, false, false, true},
    {SIP_HDR_PRIORITY, NULL, false, false, false},
    {SIP_HDR_PRIV_ANSWER_MODE, NULL, false, false, false},
    {SIP_HDR_PRIVACY, NULL, false, false, true},
    {SIP_HDR_PROXY_AUTHENTICATE, NULL, false, false, false},
    {SIP_HDR_PROXY_AUTHORIZATION, NULL, false, false, false},
    {SIP_HDR_PROXY_REQUIRE, NULL, false, false, true},
    {SIP_HDR_RACK, NULL, false, false, false},
    {SIP_HDR_REASON, NULL, false, false, true},
    {SIP_HDR_RECORD_ROUTE, sipHdrGenericNameParam_encode, false, true, true},
    {SIP_HDR_REFER_SUB, NULL, false, false, false},
    {SIP_HDR_REFER_TO, NULL, false, false, false},
    {SIP_HDR_REFERRED_BY, NULL, false, false, false},
    {SIP_HDR_REJECT_CONTACT, NULL, false, false, true},
    {SIP_HDR_REPLACES, NULL, false, false, false},
    {SIP_HDR_REPLY_TO, NULL, false, false, false},
    {SIP_HDR_REQUEST_DISPOSITION, NULL, false, false, true},
    {SIP_HDR_REQUIRE, NULL, false, false, true},
    {SIP_HDR_RESOURCE_PRIORITY, NULL, false, false, true},
    {SIP_HDR_RESPONSE_KEY, NULL, false, false, },
    {SIP_HDR_RETRY_AFTER, NULL, false, false, false},
    {SIP_HDR_ROUTE, sipHdrGenericNameParam_encode, false, true, true},
    {SIP_HDR_RSEQ, NULL, false, false, false},
    {SIP_HDR_SECURITY_CLIENT, NULL, false, false, true},
    {SIP_HDR_SECURITY_SERVER, NULL, false, false, true},
    {SIP_HDR_SECURITY_VERIFY, NULL, false, false, true},
    {SIP_HDR_SERVER, NULL, false, false, true},
    {SIP_HDR_SERVICE_ROUTE, NULL, false, false, true},
    {SIP_HDR_SESSION_EXPIRES, sipHdrLenTime_encode, false, false, false},
    {SIP_HDR_SIP_ETAG, NULL, false, false, false},
    {SIP_HDR_SIP_IF_MATCH, NULL, false, false, false},
    {SIP_HDR_SUBJECT, NULL, false, false, false},
    {SIP_HDR_SUBSCRIPTION_STATE, NULL, false, false, false},
    {SIP_HDR_SUPPORTED, sipHdrName_encode, false, false, true},
    {SIP_HDR_TARGET_DIALOG, NULL, false, false, false},
    {SIP_HDR_TIMESTAMP, NULL, false, false, false},
    {SIP_HDR_TO, sipHdrTo_encode, true, true, false},
    {SIP_HDR_TRIGGER_CONSENT, NULL, false, false, true},
    {SIP_HDR_UNSUPPORTED, NULL, false, false, true},
    {SIP_HDR_USER_AGENT, NULL, false, false, true},
    {SIP_HDR_VIA, sipHdrVia_encode, true, true, true},
    {SIP_HDR_WARNING, NULL, false, false, true},
    {SIP_HDR_WWW_AUTHENTICATE, NULL, false, false, false},
};


#endif
