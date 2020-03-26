#include "string.h"
#include "osDebug.h"
#include "osPL.h"
#include "sipHdrDate.h"



osStatus_e sipParserHdr_Date(osMBuf_t* pSipMsg, size_t hdrEndPos, sipHdrDate_t* pDate)
{
    DEBUG_BEGIN

    osStatus_e status = OS_STATUS_OK;

	if(!pSipMsg || !pDate)
	{
		logError("NULL pointer, pSipMsg=%p, pDate=%p.", pSipMsg, pDate);
		goto EXIT;
	}

    if(hdrEndPos - pSipMsg->pos < 29)
	{
		logError("the Date header length is less than 29 characters.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}

	pDate->wkday.p = &pSipMsg->buf[pSipMsg->pos];
	pSipMsg->pos +=3;
	if(pSipMsg->buf[pSipMsg->pos] != ',' && pSipMsg->buf[pSipMsg->pos+1] != ' ')
	{
		logError("incorrect wkday format.");
		status = OS_ERROR_INVALID_VALUE;
		goto EXIT;
	}
	pDate->wkday.l =3;
	pSipMsg->pos += 2;

	pDate->day.p = &pSipMsg->buf[pSipMsg->pos];
	pSipMsg->pos +=2;
	if(pSipMsg->buf[pSipMsg->pos] != ' ')
    {
        logError("incorrect day format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
	pDate->day.l =2;
    pSipMsg->pos++;

    pDate->month.p = &pSipMsg->buf[pSipMsg->pos];
    pSipMsg->pos +=3;
    if(pSipMsg->buf[pSipMsg->pos] != ' ')
    {
        logError("incorrect month format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pDate->month.l =3;
    pSipMsg->pos++;

    pDate->year.p = &pSipMsg->buf[pSipMsg->pos];
    pSipMsg->pos +=4;
    if(pSipMsg->buf[pSipMsg->pos] != ' ')
    {
        logError("incorrect year format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pDate->year.l =4;
    pSipMsg->pos++;

    pDate->hour.p = &pSipMsg->buf[pSipMsg->pos];
    pSipMsg->pos +=2;
    if(pSipMsg->buf[pSipMsg->pos] != ':')
    {
        logError("incorrect hour format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pDate->hour.l =2;
    pSipMsg->pos++;

    pDate->min.p = &pSipMsg->buf[pSipMsg->pos];
    pSipMsg->pos +=2;
    if(pSipMsg->buf[pSipMsg->pos] != ':')
    {
        logError("incorrect min format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pDate->min.l =2;
    pSipMsg->pos++;
	
    pDate->sec.p = &pSipMsg->buf[pSipMsg->pos];
    pSipMsg->pos +=2;
    if(pSipMsg->buf[pSipMsg->pos] != ' ')
    {
        logError("incorrect sec format.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pDate->sec.l =2;
    pSipMsg->pos++;

    if(strncmp(&pSipMsg->buf[pSipMsg->pos], "GMT", 3) !=0)
	{
		logError("the date is not GMT.");
        status = OS_ERROR_INVALID_VALUE;
        goto EXIT;
    }
    pSipMsg->pos +=3;

EXIT:
    DEBUG_END
    return status;
}

