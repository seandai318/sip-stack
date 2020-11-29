#include "cscfConfig.h"


void cscf_init(char* cscfConfigFolder)
{
    cscfConfig_init(cscfConfigFolder);

	icscf_init(ICSCF_HASH_SIZE);
    scscf_init(SCSCF_HASH_SIZE);
}

