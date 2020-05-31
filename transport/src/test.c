#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <endian.h>



void main()
{
	struct sockaddr_in saddr;

	inet_pton(AF_INET, "10.221.138.178", &saddr.sin_addr.s_addr);

	printf("AF_INET=%d, ip=0x%x\n", AF_INET, saddr.sin_addr.s_addr);
    printf("AF_INET=%d, ip=0x%x\n", AF_INET, htobe32(saddr.sin_addr.s_addr));
}


