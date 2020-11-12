
sipResponse_e scscf_getRegExpireFromMsg(sipMsgDecodedRawHdr_t* pReqDecodedRaw, uint32_t* pRegExpire, sipHdrDecoded_t** ppContactHdr)
{
	*ppContactHdr = NULL;
	rspCode = SIP_RESPONSE_INVALID;

    //check the expire header
    bool isExpiresFound = false;
    if(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES] != NULL)
    {
        sipHdrDecoded_t expiryHdr;
        status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_EXPIRES]->pRawHdr, &expiryHdr, false);
        if(status != OS_STATUS_OK)
        {
            logError("fails to get expires value from expires hdr by sipDecodeHdr.");
            rspCode = SIP_RESPONSE_400;
            goto EXIT;
        }

        *pRegExpire = *(uint32_t*)expiryHdr.decodedHdr;
        osfree(expiryHdr.decodedHdr);
        isExpiresFound = true;
    }
    else
    {
    	*ppContactHdr = oszalloc(sizeof(sipHdrDecoded_t), sipHdrDecoded_cleanup);
    	status = sipDecodeHdr(pReqDecodedRaw->msgHdrList[SIP_HDR_CONTACT]->pRawHdr, *ppContactHdr, true);
    	if(status != OS_STATUS_OK)
    	{
        	logError("fails to decode contact hdr in sipDecodeHdr.");
        	rspCode = SIP_RESPONSE_400;
			*ppContactHdr = osfree(*ppContactHdr);
        	goto EXIT;
    	}

        osPointerLen_t expireName={"expires", 7};
        //pContactExpire is not allocated an new memory, it just refer to a already allocated memory in pGNP->hdrValue, no need to dealloc memory for pContactExpire
        pContactExpire = sipHdrGenericNameParam_getGPValue(&((sipHdrMultiContact_t*)*ppContactHdr->decodedHdr)->contactList.pGNP->hdrValue, &expireName);
        if(pContactExpire != NULL)
        {
            isExpiresFound = true;
            *pRegExpire = osPL_str2u32(pContactExpire);
        }
    }

    if(!isExpiresFound)
    {
        *pRegExpire = SCSCF_REG_DEFAULT_EXPIRE;
    }

    if(*pRegExpire != 0 && *pRegExpire < SCSCF_REG_MIN_EXPIRE)
    {
        *pRegExpire = SCSCF_REG_MIN_EXPIRE;
        rspCode = SIP_RESPONSE_423;
        goto EXIT;
    }
    else if (regExpire > SIP_REG_MAX_EXPIRE)
    {
        regExpire = SIP_REG_MAX_EXPIRE;
        if(pContactExpire)
        {
            osPL_modifyu32(pContactExpire, regExpire);
        }
    }

EXIT:
	return rspCode;
}
