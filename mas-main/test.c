#include <string.h>
#include <stdio.h>
#include "osMBuf.h"
#include "sipHeader.h"
#include "sipHdrGperfHash.h"
#include "osDebug.h"
#include "sipHdrNameValue.h"
#include "sipMsgRequest.h"
#include "sipProxy.h"
#include "osPL.h"
#include "osList.h"
#include "osTypes.h"
#include "sipTransIntf.h"
#include "sipTransport.h"
#include "masMgr.h"


int main(int argc, char* argv[])
{
    if(argc !=2)
    {
        printf("need to input the file name.");
        return 1;
    }

    osDbg_init(DBG_DEBUG, DBG_ALL);

    char sipMsg[3000];
    char* p = &sipMsg[0];
    FILE* fp = fopen(argv[1], "rb");
    char c;
    while((c=getc(fp)) != EOF)
    {
        //when read from a file, a new line only has '\n', no '\r'
        if(c == '\n')
        {
            *p++ = '\r';
        }
        *p++ = c;

    }
    *p='\0';

    printf("sipMsg=\n%s", sipMsg);

    osMBuf_t* sipMBuf = osMBuf_alloc(3000);
    osMBuf_writeStr(sipMBuf, sipMsg, false);

	sipTransInit(1000);
	masInit();


	osStatus_e status = OS_STATUS_OK;
	status = sipTrans_onMsg(SIP_TRANS_MSG_TYPE_PEER, sipMBuf, 0);
	if(status != OS_STATUS_OK)
	{
		logError("sipTrans_onMsg fails, status=%d.", status);
		return -1;
	}

	return 1;
}
