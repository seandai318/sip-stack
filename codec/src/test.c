#include <stdio.h>

void test(int* tt)
{
	*tt=1;
}

void main()
{
	int i=10;
	test(&i);
	printf("tt=%d\n", i);
}
