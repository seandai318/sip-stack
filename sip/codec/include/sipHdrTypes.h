#ifndef _SIP_HDR_TYPES_H
#define _SIP_HDR_TYPES_H

#include "osTypes.h"
#include "osPL.h"
#include "osList.h"
#include "osMBuf.h"

#include "sipHeaderData.h"
#include "sipParamNameValue.h"
#include "sipUri.h"


//hdr's startPos and len.  For a hdr that have multiple values sharing one hdr name, each value has its own startPos and len,
//for the 1st hdr value, the start pos points to the beginnig of no WS hdr character, the hdr name is not included.
//for the other hdr value, the startPos points to the first char after the ',',
//a value's len ending with ',', and ',' is included in the value's len.  for the last value, it ends with \r\n, and \r\n is included in the last value's length
//example: a raw header of "hdr-a : abc , def, ghi\r\n", say the starting pos for this raw hdr is 60.  There will be three sipHdr_posInfo for this raw hdr, the first one: startPos=68, len=5; the 2nd one: startPos=73, len=5; the 3rd one: startPos=78, len=6.
typedef struct sipHdr_posInfo {
    size_t startPos;
    size_t totalLen;
//  osPointerLen_t value;   //only include value itself, does not include \r\n, may include SP.
} sipHdr_posInfo_t;


//example: "Conten-Length: 12345"
#define sipHdrInt_t	uint32_t

//example: Session-Expires: 4000;refresher=uac
//example: "Retry-After: 18000 (in maintainance);duration=3600"
typedef struct sipHdrIntParam {
    uint32_t number;
	osPointerLen_t comment;
	uint8_t paramNum;
    osPointerLen_t param;
	osList_t paramList;		//each element contains osPointerLen_t*
} sipHdrIntParam_t;


//example: "CallId: 123@567"
#define sipHdrStr_t osPointerLen_t


//example: "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY"
typedef struct sipHdrNameList {
    uint8_t nNum;
    osList_t nameList;      //each element contains a osPointerLen_t or sipParamName_t
} sipHdrNameList_t;


//example: "AuthenticationInfo: nextnonce="47364c23432d2e131a5fb210812c""
typedef struct sipHdrNameValueList {
    uint8_t nvpNum;
    sipParamNameValue_t* pNVP;
    osList_t nvpList;        //each element contains a sipParamNameValue_t*
} sipHdrNameValueList_t;


typedef struct sipHdrNameValueListDecoded {
    sipHdr_posInfo_t hdrPos;
    sipHdrNameValueList_t hdrValue;
} sipHdrNameValueListDecoded_t;


//example: "P-Charging-Function-Addresses: ccf=192.1.1.1;ccf=192.1.1.2;aa, ecf=10.1.2.3;bb"
//sipHdrMultiNameValueList_t is very similar to sipHdrMultiValueParam_t, except that sipHdrMultiValueParam_t has a value (like ae1 in the example)
typedef struct sipHdrMultiNameValueList {
	uint8_t nvNum;
	sipHdrNameValueListDecoded_t* pNV;
	osList_t nvList;		//stores the 2nd and forward hdr values for a header entry, each element contains sipHdrNameValueListDecoded_t
} sipHdrMultiNameValueList_t;


//example: "Authorization: Digest userName="bob", realm="a.com", uri="b@c.com""
typedef struct sipHdrMethodParam {
    osPointerLen_t method;
    sipHdrNameValueList_t nvParamList;
} sipHdrMethodParam_t;


//example: "Content-Disposition: session;handling="optional";aa"
typedef struct sipHdrValueParam {
	osPointerLen_t value;
	sipHdrNameValueList_t nvParamList;
} sipHdrValueParam_t;


typedef struct sipHdrValueParamDecoded {
	sipHdr_posInfo_t hdrPos;
	sipHdrValueParam_t hdrValue;
} sipHdrValueParamDecoded_t;


//example: "Accept-Encoding: *;q=0.1;a=b, ae1;q=0.2;c; ae2;d=f; gzip"
typedef struct sipHdrMultiValueParam {
	uint8_t vpNum;
	sipHdrValueParamDecoded_t* pVP;
	osList_t vpList;		//stores the 2nd and forward hdr values for a header entry, each element contains sipHdrValueParamDecoded_t*
} sipHdrMultiValueParam_t;


//example: "Content-Type: application/sdp;aa==bb"
typedef struct sipHdrSlashValueParam {
	osPointerLen_t mType;
	osPointerLen_t mSubType;
	sipHdrNameValueList_t paramList;
} sipHdrSlashValueParam_t;


typedef struct sipHdrSlashValueParamDecoded {
	sipHdr_posInfo_t hdrPos;
	sipHdrSlashValueParam_t hdrValue;
} sipHdrSlashValueParamDecoded_t;


//example: "Accept: */*;a=b, text/*;c=d;e=f, message/rr;g=h"
typedef struct sipHdrMultiSlashValueParam {
	uint8_t svpNum;
	sipHdrSlashValueParamDecoded_t* pSVP;
	osList_t svpList;	//stores the 2nd and forward hdr values for a header entry, each element contains sipHdrSlashValueParamDecoded_t*
} sipHdrMultiSlashValueParam_t;


//example: "CSeq: 12345 INVITE"
typedef struct sipHdrCSeq {
    uint32_t seqNum;
    osPointerLen_t method;
} sipHdrCSeq_t;


//hdrs that are not to be decoded except for raw decoding the hdr name and hdr value
typedef struct sipHdrOther {
	osPointerLen_t hdrName;
	osPointerLen_t hdrValue;
} sipHdrOther_t;


typedef struct sipHdrVia {
    osPointerLen_t sentProtocol[3];
    sipHostport_t hostport;
	uint8_t paramNum;					//include branchId parameter
	sipParamNameValue_t* pBranch;		//for both decode and encode, branchId must exist
    osList_t viaParamList;  			//contains sipParamNameValue_t, for both encode and decode.
} sipHdrVia_t;


typedef struct sipHdrViaDecoded {
	sipHdr_posInfo_t hdrPos;
	sipHdrVia_t hdrValue;
} sipHdrViaDecoded_t;


typedef struct sipHdrMultiVia {
	uint8_t viaNum;
	sipHdrViaDecoded_t* pVia;	//stores the first via hdr.  if a via hdr name has more than 1 value, starting from the 2nd one, stored in viaList
    osList_t viaList;   //stores the 2nd and forward hdr values for a header entry, each element contains a pDecodedVia
} sipHdrMultiVia_t;


typedef struct sipHdr_genericNameParam {
    osPointerLen_t displayName;
    sipUri_t uri;
    osList_t genericParam;      //for both decode and encode, each element is sipParamNameValue_t
} sipHdrGenericNameParam_t;


typedef struct sipHdr_genericNameParamDecoded {
    sipHdr_posInfo_t hdrPos;
    sipHdrGenericNameParam_t hdrValue;
} sipHdrGenericNameParamDecoded_t;


typedef struct sipHdrMultiGenericNameParam {
    uint8_t gnpNum;
    sipHdrGenericNameParamDecoded_t* pGNP;  //the first gnp hdr, if a hdr name has more than 1 value, starting from the 2nd one, stored in gnpList
    osList_t gnpList;   //stores the 2nd and forward hdr values for a header entry, each element contains sipHdrGenericNameParamDecoded_t* 
} sipHdrMultiGenericNameParam_t;


typedef struct sipHdrContact {
	bool isStar;
	sipHdrMultiGenericNameParam_t contactList;
} sipHdrMultiContact_t;


void* sipHdrParse(osMBuf_t* pSipMsg, sipHdrName_e hdrNameCode, size_t hdrValuePos, size_t hdrValueLen);
void* sipHdrParseByName(osMBuf_t* pSipRawHdr, sipHdrName_e hdrNameCode);
void sipHdrIntParam_cleanup(void* data);
void sipHdrNameList_cleanup(void* data);
void sipHdrNameValueList_cleanup(void* data);
void sipHdrNameValueListDecoded_cleanup(void* data);
void sipHdrMultiNameValueList_cleanup(void* data);
void sipHdrValueParam_cleanup(void* data);
void sipHdrValueParamDecoded_cleanup(void* data);
void sipHdrMethodParam_cleanup(void* data);
void sipHdrSlashValueParam_cleanup(void* data);
void sipHdrSlashValueParamDecoded_cleanup(void* data);
void sipHdrMultiSlashValueParam_cleanup(void* data);
void sipHdrGenericNameParam_cleanup(void* data);
void sipHdrGenericNameParamDecoded_cleanup(void* data);
void sipHdrMultiGenericNameParam_cleanup(void* data);
void sipHdrMultiValueParam_cleanup(void* data);
void sipHdrMultiContact_cleanup(void* data);
void sipHdrVia_cleanup(void* data);
void sipHdrViaDecoded_cleanup(void* data);
void sipHdrMultiVia_cleanup(void* data);

	
#define sipHdrType_accept_t						sipHdrMultiSlashValueParam_t	//example: "Accept: */*;a=b, text/*;c=d;e=f, message/rr;g=h"
#define sipHdrType_acceptContact_t				sipHdrMultiGenericNameParam_t
#define sipHdrType_acceptEncoding_t				sipHdrMultiValueParam_t			//example: "Accept-Encoding: *;q=0.1;a=b, ae1;q=0.2;c; ae2;d=f; gzip"
#define sipHdrType_acceptLanguage_t				sipHdrMultiValueParam_t			//example: "da, en-gb;q=0.8;a, en;q=0.7;b=c"
#define sipHdrType acceptResourcePriority_t		sipHdrNameList_t					//example: "dsn.flash-override, dsn.flash, dsn.immediate"
#define sipHdrType_alertInfo_t					sipHdrMultiGenericNameParam_t	//example: "<sip:abc@efg.com>;ttt, <sip:efg@hij.com>", do not support absoluteURI
#define sipHdrType_allow_t						sipHdrNameList_t					//example: "Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY"
#define sipHdrType_allowEvents_t				sipHdrNameList_t					//example: "Allow-Events: spirits-INDPs"
#define sipHdrType_answerMode_t					sipHdrValueParam_t				//example: "Answer-Mode: Auto;aa;bb"
#define sipHdrType_authenticationInfo_t			sipHdrNameValueList_t			//example: "AuthenticationInfo: nextnonce="47364c23432d2e131a5fb210812c""
#define sipHdrType_authorization_t				sipHdrMethodParam_t				//example: "Authorization: Digest userName="bob", realm="a.com", uri="b@c.com""
#define sipHdrType_callId_t						sipHdrStr_t
#define sipHdrType_callInfo_t					sipHdrMultiGenericNameParam_t	//example: "Call-Info: "<sip:a@b>;purpose=c;d=e;f, <http://h@i>;purpose=info"
#define sipHdrType_contact_t					sipHdrMultiContact_t
#define sipHdrType_contentDisposition_t			sipHdrValueParam_t				//example: "Content-Disposition: session;handling="optional" 
#define sipHdrType_contentEncoding_t			sipHdrNameList_t					//example: "Content-Encoding: gzip, aaa"	
#define sipHdrType_contentLanguage_t			sipHdrNameList_t					//example: "Content-Language: fr, cn-ca"
#define sipHdrType_contentLength_t				sipHdrInt_t						//examlpe: "Conten-Length: 12345"
#define sipHdrType_contentType_t				sipHdrSlashValueParam_t			//example: "Content-Type: application/sdp;aa==bb"
#define sipHdrType_cSeq_t						sipHdrCSeq_t
#define sipHdrType_data_t						sipHdrStr_t						//example: "Date: Thu, 21 Feb 2002 13:02:03 GMT"
#define sipHdrType_encryption_t					sipHdrMethodParam_t				//example: "Encryption: PGP version=2.6.2,encoding=ascii"	only in 2543, obsolete
#define sipHdrType_errorInfo_t					sipHdrMultiGenericNameParam_t	//example: "Error-Info: <sips:screen-failure-term-ann@annoucement.example.com>"
#define sipHdrType_event_t						sipHdrValueParam_t				//example: "Event: refer;aa=bb"
#define sipHdrType_expires_t					sipHdrInt_t
#define sipHdrType_flowTimer_t					sipHdrInt_t
#define sipHdrType_from_t						sipHdrGenericNameParam_t
#define sipHdrType_hide_t						sipHdrOther_t					//only in 2543, obsolete
#define sipHdrType_historyInfo_t				sipHdrMultiGenericNameParam_t
#define sipHdrType_identity_t					sipHdrValueParam_t
#define sipHdrType_identityInfo_t				sipHdrOther_t					//Deprecated by:  RFC 8224 – Section 13.1
#define sipHdrType_inReplyTo_t					sipHdrNameList_t					//example: "In-Reply-To: 70710@saturn.bell-tel.com, 17320@saturn.bell-tel.com"
#define sipHdrType_join_t						sipHdrValueParam_t				//example: "Join: 12345600@atlanta.example.com;from-tag=1234567;to-tag=23431"
#define sipHdrType_maxBreadth_t					sipHdrInt_t
#define sipHdrType_maxForwards_t				sipHdrInt_t
#define sipHdrType_mimeVersion_t				sipHdrStr_t
#define sipHdrType_minExpires_t					sipHdrInt_t
#define sipHdrType_minSE_t						sipHdrInt_t
#define sipHdrType_organization_t				sipHdrStr_t
#define sipHdrType_pAccessNetworkInfo_t			sipHdrMultiValueParam_t
#define sipHdrType_pAnswerState_t				sipHdrValueParam_t
#define sipHdrType_pAssertedIdentity_t			sipHdrMultiGenericNameParam_t
#define sipHdrType_pAssociatedUri_t				sipHdrMultiGenericNameParam_t
#define sipHdrType_pCalledPartyId_t				sipHdrGenericNameParam_t
#define sipHdrType_pChargingFunctionAddresses_t	sipHdrMultiNameValueList_t		//example: "P-Charging-Function-Addresses: ccf=192.1.1.1;ccf=192.1.1.2;aa, ecf=10.1.2.3;bb"
#define sipHdrType_pChargingVector_t			sipHdrNameValueList_t			//example: "P-Charging-Vector: icid-value=1234bc9876e;icid-generated-at=192.0.6.8; orig-ioi=home1.net"
#define sipHdrType_pDcsTracePartyId_t			sipHdrOther_t
#define sipHdrType_pDcsOsps_t					sipHdrOther_t
#define sipHdrType_pDcsBillingInfo_t			sipHdrOther_t
#define sipHdrType_pDcsLaes_t					sipHdrOther_t
#define sipHdrType_pDcsRedirect_t				sipHdrOther_t
#define sipHdrType_pEarlyMedia_t				sipHdrNameList_t
#define sipHdrType_pMediaAuthorization_t		sipHdrNameList_t
#define sipHdrType_pPreferredIdentity_t			sipHdrMultiGenericNameParam_t
#define sipHdrType_pProfileKey_t				sipHdrGenericNameParam_t
#define sipHdrType_pRefusedUriList_t			sipHdrGenericNameParam_t
#define sipHdrType_pServedUser_t				sipHdrGenericNameParam_t
#define sipHdrType_pUserDatabase_t				sipHdrGenericNameParam_t
#define sipHdrType_pVisitedNetworkId_t			sipHdrMultiValueParam_t			//example: "P-Visited-Network-ID: other.net;aa, "visitednetwork 1";bb;cc"
#define sipHdrType_path_t						sipHdrMultiGenericNameParam_t
#define sipHdrType_permissionMissing_t			sipHdrMultiGenericNameParam_t	//example: "Permission-Missing: sip:C@example.com"
#define sipHdrType_priority_t					sipHdrStr_t						//example: "Priority: emergency"
#define sipHdrType_privAnswerMode_t				sipHdrValueParam_t				//example: "Priv-Answer-Mode: Auto"
#define sipHdrType_privacy_t					sipHdrNameValueList_t			//example: "Privacy: id;user"
#define sipHdrType_proxyAuthenticate_t			sipHdrMethodParam_t				//example: "Proxy-Authenticate: Digest realm="atlanta.example.com", qop="auth", algorithm=MD5"
#define sipHdrType_proxyAuthorization_t			sipHdrMethodParam_t				//example: "Proxy-Authenticate: Digest realm="atlanta.example.com", qop="auth", response="aaa""
#define sipHdrType_proxyRequire_t				sipHdrNameList_t					//example: "Proxy-Require: sec-agree"
#define sipHdrType_rAck_t						sipHdrStr_t
#define sipHdrType_reason_t						sipHdrMultiValueParam_t			//example: "Reason: Q.850;cause=16;text="Terminated", Reason: SIP;cause=600;text="Busy Everywhere""
#define sipHdrType_recordRoute_t				sipHdrMultiGenericNameParam_t
#define sipHdrType_referSub_t					sipHdrValueParam_t				//example: "Refer-Sub: true;aaa;bbb=c"
#define sipHdrType_referTo_t					sipHdrGenericNameParam_t		//example: "Refer-To: <sip:abc@example.com?Replaces=efg%40example.com%3Bfrom-tag%3Da2b%3Bto-tag%3D12>"
#define sipHdrType_referredBy_t					sipHdrGenericNameParam_t		//example: "Referred-By: <sip:referrer@example.com>;cid="20398823.2UWQFN309shb3@referrer.example""
#define sipHdrType_rejectContact_t				sipHdrMultiValueParam_t			//example: "Reject-Contact: *;actor="msg-taker";video"
#define sipHdrType_replaces_t					sipHdrValueParam_t				//example: "Replaces: 425928@bobster.example.org;to-tag=7743;from-tag=6472"
#define sipHdrType_replyTo_t					sipHdrGenericNameParam_t		//example: "Reply-To: Bob <sip:bob@biloxi.com>"						
#define sipHdrType_requestDisposition_t			sipHdrNameList_t					//example: "Request-Disposition: proxy, recurse, parallel"
#define sipHdrType_require_t					sipHdrNameList_t					//example: "Require: 100rel"
#define sipHdrType_resourcePriority_t			sipHdrNameList_t					//example: "Resource-Priority: wps.3, dsn.flash"
#define sipHdrType_responseKey_t				sipHdrOther_t					//only in 2543, obsolete
#define sipHdrType_retryAfter_t					sipHdrIntParam_t				//example: "Retry-After: 18000 (in maintainance);duration=3600"
#define sipHdrType_route_t						sipHdrMultiGenericNameParam_t
#define sipHdrType_rSeq_t						sipHdrInt_t						//example: "RSeq: 988789"
#define sipHdrType_securityClient_t				sipHdrMultiValueParam_t
#define sipHdrType_securityServer_t				sipHdrMultiValueParam_t			//example: "Security-Server: ipsec-ike;q=0.1"
#define sipHdrType_securityVerify_t				sipHdrMultiValueParam_t			//example: "Security-Verify: tls;q=0.2"
#define sipHdrType_server_t						sipHdrStr_t
#define sipHdrType_serviceRoute_t				sipHdrMultiGenericNameParam_t
#define sipHdrType_sessionExpires_t				sipHdrIntParam_t				//example: "Session-Expires: 4000;refresher=uac"
#define sipHdrType_sipETag_t					sipHdrStr_t						//example: "SIP-ETag: dx200xyz"
#define sipHdrType_sipIfMatch_t					sipHdrStr_t						//example: "SIP-If-Match: dx200xyz"
#define sipHdrType_subject_t					sipHdrStr_t						//example: "Subject: A tornado is heading our way!"
#define sipHdrType_subscriptionState_t			sipHdrValueParam_t				//example: "Subscription-State: active;expires=60"
#define sipHdrType_supported_t					sipHdrNameList_t					//example: "Supported: replaces"
#define sipHdrType_targetDialog_t				sipHdrValueParam_t
#define sipHdrType_timeStamp_t					sipHdrStr_t						//example: "Timestamp: 54.20 delay
#define sipHdrType_to_t							sipHdrGenericNameParam_t
#define sipHdrType_triggerConsent_t				sipHdrMultiGenericNameParam_t	//example: "Trigger-Consent: sip:123@relay.example.com;target-uri="sip:friends@relay.example.com""
#define sipHdrType_unsupported_t				sipHdrNameList_t					//example: "Unsupported: 100rel"
#define sipHdrType_userAgent_t					sipHdrStr_t						//example: "User-Agent: Softphone Beta1.5"
#define sipHdrType_via_t						sipHdrMultiVia_t
#define sipHdrType_warning_t					sipHdrOther_t					//do not implement, just pass
#define sipHdrType_wwwAuthenticate_t			sipHdrMethodParam_t


#endif
