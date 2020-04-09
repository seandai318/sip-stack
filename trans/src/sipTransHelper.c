#include "osTypes.h"
#include "osDebug.h"
#include "osMBuf.h"
#include "osPL.h"

#include "sipHeader.h"


//assume the pSipSrcReq is a request.  there will be no further check
osStatus_e sipTransHelper_reqlineReplaceAndCopy(osMBuf_t* pDestMsg, osMBuf_t* pSipSrcReq, osPointerLen_t* pMethod)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pDestMsg || !pSipSrcReq || !pMethod)
	{
		logError("null pointer, pDestMsg=%p, pSipSrcReq=%p, pMethod=%p.",  pDestMsg, pSipSrcReq, pMethod);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	size_t startPos = 0;
	size_t stopPos = 0;
	//to check the empty space before method or response line
	bool isPre = true;
	int pos = 0;
	for(; pos<pSipSrcReq->end; pos++)
	{
		if(isPre && (pSipSrcReq->buf[pos] == ' ' || pSipSrcReq->buf[pos] == '\t'))
		{
		continue;
		}

		isPre = false;

		if(startPos == 0 && (pSipSrcReq->buf[pos] = ' ' || pSipSrcReq->buf[pos] == '\t'))
		{
			startPos = pos;
		}

		if(pSipSrcReq->buf[pos] == '\r' && pSipSrcReq->buf[pos+1] == '\n')
		{
			stopPos = pos+2;
			break;
		}

	}

	if(pos == pSipSrcReq->end-1 || stopPos <= startPos)
	{
		logError("fails to find a request line, this shall never happen.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pDestMsg->pos = 0;
	osMBuf_writePL(pDestMsg, pMethod, true);
	osPointerLen_t reqLine = {&pSipSrcReq->buf[startPos], stopPos - startPos};
	osMBuf_writePL(pDestMsg, &reqLine, true);

EXIT:
	return status;
}


osStatus_e sipTransHelper_cSeqReplaceAndCopy(osMBuf_t* pDestMsg, sipRawHdr_t* pCSeqRawHdr, osPointerLen_t* pMethod)
{
	osStatus_e status = OS_STATUS_OK;

	if(!pDestMsg || !pCSeqRawHdr || !pMethod)
	{
		logError("null pointer, pDestMsg=%p, pCSeqRawHdr=%p, pMethod=%p.", pDestMsg, pCSeqRawHdr, pMethod);
		status = OS_ERROR_NULL_POINTER;
		goto EXIT;
	}

	int i=0;
	for(;i<pCSeqRawHdr->value.l; i++)
	{
		if(pCSeqRawHdr->value.p[i] == ' ' || pCSeqRawHdr->value.p[i] == '\t')
		{
			break;
		}
	}

	if(i == pCSeqRawHdr->value.l - 1)
	{
		logError("fails to find method in CSeq, this shall never happen.");
		status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }

	osMBuf_writeStr(pDestMsg, "CSeq: ", true);

    osPointerLen_t cSeqNum = {pCSeqRawHdr->value.p, i+1};
	osMBuf_writePL(pDestMsg, &cSeqNum, true);
	osMBuf_writePL(pDestMsg, pMethod, true);
	osMBuf_writeStr(pDestMsg, "\r\n", true);

EXIT:
	return status;
}
	
	

