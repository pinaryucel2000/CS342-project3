#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <stdlib.h>
#include <math.h>
#include "sbmem.h"

#define size 32768

void experiment(int s, int r)
{
	printf("Request Size: %d\n", s);
	void* arr[r]; 
	int i;
	
	for(i = 0; i < r; i++)
	{
		arr[i] = sbmem_alloc(s);
	}
	
	printInternalFrag();

	printf("Freeing half...\n");
	for(i = 0; i < r ; i = i+2)
	{
		sbmem_free(arr[i]);
	}
	
	printExternalFrag();
	
	for(i = 1; i < r ; i = i+2)
	{
		sbmem_free(arr[i]);
	}
	printf("\n");
}

int main()
{
	pid_t n;
	
	n = fork();
	if(n == 0)
	{
		sbmem_open();
		experiment(4000, 8);
		sbmem_close();
		exit(0);	
	}

	wait(NULL);
	
	n = fork();
	if(n == 0)
	{
		sbmem_open();
		experiment(3000, 8);
		sbmem_close();
		exit(0);	
	}

	wait(NULL);
	
	n = fork();
	if(n == 0)
	{
		sbmem_open();
		experiment(2000, 16);
		sbmem_close();
		exit(0);	
	}

	wait(NULL);
	
	n = fork();
	if(n == 0)
	{
		sbmem_open();
		experiment(1000, 32);
		sbmem_close();
		exit(0);	
	}
	
	wait(NULL);
	
	sbmem_open();
	experiment(994, 32);
	sbmem_close();

	sbmem_open();
	experiment(964, 32);
	sbmem_close();

	sbmem_open();
	experiment(934, 32);
	sbmem_close();
	
	sbmem_open();
	experiment(226, 128);
	sbmem_close();
	
	sbmem_open();
	experiment(196, 128);
	sbmem_close();
	
	sbmem_open();
	experiment(166, 128);
	sbmem_close();

	wait(NULL);

	return 0;	
}





