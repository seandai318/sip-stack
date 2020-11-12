#include "osTypes.h"
#include "osPL.h"

#include "sipHeader.h"
#include "sipHdrNameValue.h"



osStatus_e cscf_getImpiFromSipMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, osPointerLen_t* pImpu, osPointerLen_t* pImpi)
{
	osStatus_e status = OS_STATUS_OK;

    if(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION])
    {
        sipHdrDecoded_t authHdrDecoded={};
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_AUTHORIZATION]->pRawHdr, &authHdrDecoded, true);
        if(status != OS_STATUS_OK)
        {
            logError("fails to decode authorization hdr in sipDecodeHdr.");
            goto EXIT;
        }

        osPointerLen_t username = {"username", sizeof(userName)-1};
        osPointerLen_t* pImsiFromMsg = sipHdrNameValueList_getValue(&((sipHdrMethodParam_t*)authHdrDecoded.decodedHdr)->nvParamList, &username);
		*pImsi = *pImsiFromMsg;
    }

    //if not pImsi, convert impu to impi
    if(!pImsi)
    {
        osPL_shiftcpy(pImpi, &sipUri, sizeof("sip:")-1);
    }

EXIT:
	return status;
}

