#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include "sbmem.h"

// Define a name for your shared memory; you can give any name that start with a slash character; it will be like a filename.
#define name "/os"

// Define your stuctures and variables. 
#define minSegmentSize 32768
#define maxSegmentSize 262144
#define kMin 8
#define kMax 18

struct info
{
	int next;
	int prev;
	int k; // indicates 2^k size
};

struct sharedSegmentInfo
{
	int k;
	int avail[kMax-kMin+1];
	sem_t sem;
	sem_t semAlloc;
	sem_t semFree;
};

int shm_fd = -1;
int internalFragmentation = 0;
void* memory;
struct sharedSegmentInfo* ssi;


void setAsAvail(struct info* block)
{
	int k = block->k;
	
	if(getAvail(k) == -1)
	{
		setAvail(k, pto(block));
		block->next = pto(block);
		block->prev = pto(block);
		return;
	}
	
	struct info* b = otp(getAvail(k));
	
	// 1 node in the list
	if(otp(b->next) == b)
	{
		b->next = pto(block);
		b->prev = b->next;
		block->next = pto(b);
		block->prev = block->next;
		
		if((uintptr_t)b > (uintptr_t)block)
		{
			setAvail(k, pto(block));
		}
		
		return;
	}	
	
	struct info* head = b;
	while((uintptr_t)b < (uintptr_t)block && otp(b->next) != head)
	{
		b = otp(b->next);
	}

	block->next = pto(b);
	block->prev = b->prev;
	(otp(b->prev))->next = pto(block);
	b->prev = pto(block);

	if(b == otp(getAvail(k)))
	{
		setAvail(k, pto(block));
		
		return;
	}	
}


void sbmem_free (void *p)
{
	sem_wait(&(ssi->semFree)); 
	p = (void*)((uintptr_t)p - (uintptr_t)sizeof(struct info));
	struct info* block = p;
	setAsAvail(block);
	
	if(ssi->k == block->k)
	{
		sem_post(&(ssi->semFree)); 
		return;
	}
	
	struct info* buddy = findBuddy(block);
	
	while(isAvail(buddy) == 1 && buddy->k == block->k)
	{
		
		removeFromAvail(buddy);
		removeFromAvail(block);
		
		if((uintptr_t)buddy > (uintptr_t)block)
		{
			block->k = block->k + 1;
			setAsAvail(block);
		}
		else
		{
			buddy->k = buddy->k + 1;
			setAsAvail(buddy);		
			block = buddy;
		}
		
		if(ssi->k == block->k)
		{
			sem_post(&(ssi->semFree)); 
			return;
		}
		
		buddy = findBuddy(block);
	}
	
	sem_post(&(ssi->semFree)); 
	
}

void printAddress(void* ptr)
{
	if(ptr == NULL)
	{
		printf("NULL\n");
		return;
	}
	
	printf("%ld\n", pto(ptr)-sizeof(struct sharedSegmentInfo) - sizeof(struct info));
}

void *sbmem_alloc (int size)
{		
	if(size < 128 || size > 4096)
	{
		return NULL;
	}
	
	sem_wait(&(ssi->semAlloc)); 

	int k = ceil(log2(size + sizeof(struct info)));
	int j = -1;
	
	int i;
	for(i = k; i <= kMax; i++)
	{
		if((j != -1 && getAvail(i) != -1 && i < j) || (j == -1 && getAvail(i) != -1))
		{
			j = i;
		}
	}

	if(j == -1)
	{
		sem_post(&(ssi->semAlloc)); 
		return NULL;
	}
	
	struct info* block = otp(getAvail(j));
	struct info* buddy;
	
	// Remove found block from available list
	removeFromAvail(block);
	
	while(j != k)
	{
		block->k = block->k - 1;
		buddy = findBuddy(block);
		
		buddy->k = block->k;
		setAsAvail(buddy);

		j--;
	}

	internalFragmentation = internalFragmentation + (int)pow(2,k) - size - sizeof(struct info);
	sem_post(&(ssi->semAlloc)); 
	
	return otp(pto(block) + sizeof(struct info));
}

int sbmem_open()
{
	internalFragmentation = 0;
	if((shm_fd = shm_open(name, O_RDWR, 0666)) == -1)
	{
		return -1;
	}
	 
	memory = mmap(0, sizeof(struct sharedSegmentInfo), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	ssi = (struct sharedSegmentInfo*) memory;
	int k = ssi->k;
	int value; 
    sem_getvalue(&(ssi->sem), &value); 
    
    if(value == 0)
    {
    	munmap(memory, sizeof(struct sharedSegmentInfo));
    	return -1;
    }
    
	sem_wait(&(ssi->sem)); 
	
	munmap(memory, sizeof(struct sharedSegmentInfo));
	memory = mmap(0, (int)pow(2, k) + sizeof(struct sharedSegmentInfo), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	ssi = (struct sharedSegmentInfo*) memory;

	return 0;
}

struct info* findBuddy(struct info* block)
{
	if(((uintptr_t)block - (uintptr_t)(memory + sizeof(struct sharedSegmentInfo))) % (uintptr_t)pow(2, block->k+1) == 0)
	{
		return  (struct info*)((uintptr_t)block + (uintptr_t)pow(2, block->k));
	}
	else
	{
		return (struct info*)((uintptr_t)block - (uintptr_t)pow(2, block->k));
	}
}

int sbmem_close()
{
	sem_post(&(ssi->sem));
	munmap(memory, (int)pow(2, ssi->k) + sizeof(struct sharedSegmentInfo));    

    return (0); 
}

int getAvail(int index)
{
	return ssi->avail[index-kMin];
}

void setAvail(int index, int newValue)
{
	ssi->avail[index-kMin] = newValue;
}

void removeFromAvail(struct info* block)
{
	int k = block->k;
	
	if(otp(block->next) == block) // 1 node
	{
		setAvail(k, -1);
	}
	else if(otp(otp(block->next)->next) == block) // 2 nodes
	{
		if(block == otp(getAvail(k)))
		{
			setAvail(k, block->next);
		}
		
		(otp(block->next))->next = block->next;
		(otp(block->prev))->prev = block->prev;
		
	}
	else // 3 or more nodes
	{
		if(block == otp(getAvail(k)))
		{
			setAvail(k, block->next);
		}
		
		(otp(block->prev))->next = block->next;
		(otp(block->next))->prev = block->prev;
	}
	
	block->next = -1;
	block->prev = -1;
}


int isAvail(struct info* block)
{
	int k = block->k;
	struct info* head = otp(getAvail(k));
	struct info* avail = head;
	
	do
	{
		if(avail == block)
		{
			return 1;
		}

		avail = otp(avail->next);
		
	}while(avail != head);
	
	return 0;
}

struct info* otp(int offset)
{
    return (struct info*)((uintptr_t)(memory) + (uintptr_t)(offset));
}

int pto(struct info* ptr)
{
    return (uintptr_t)(ptr) - (uintptr_t)(memory);
}

void printp(struct info* ptr)
{
	if(ptr == NULL)
	{
		printf("NULL\n");
	}
	else
	{
		printf("%ld\n", (uintptr_t)ptr - (uintptr_t)(memory + sizeof(struct sharedSegmentInfo)));
	}
}

int sbmem_init(int segmentsize)
{
	// Invalid segment size
	if(ceil(log2(segmentsize)) - log2(segmentsize) != 0 || segmentsize < minSegmentSize || segmentsize > maxSegmentSize)
	{
		return -1;
	}
	
	// Remove the existing shared memory segment
	if(shm_fd > 0)
	{
		if(sbmem_remove() == -1)
		{
			return -1;
		}
	}
		
	if((shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666)) == -1)
	{
		return -1;
	}
	
	if(ftruncate(shm_fd, segmentsize) == -1)
	{
		return -1;
	}
	
    
    memory = mmap(0, segmentsize + sizeof(struct sharedSegmentInfo), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    ssi = (struct sharedSegmentInfo*) memory;
    ssi->k = ceil(log2(segmentsize));
    sem_init(&(ssi->sem), 0, 10);
    sem_init(&(ssi->semAlloc), 0, 1);
    sem_init(&(ssi->semFree), 0, 1);
    
    int k;   
    for(k = kMin; k <= kMax; k++)
    {
    	setAvail(k, -1);
    }
    
    struct info* i = (struct info*) (memory + sizeof(struct sharedSegmentInfo));
    i->next = sizeof(struct sharedSegmentInfo);
    i->prev = sizeof(struct sharedSegmentInfo);
    i->k = (int)log2(segmentsize);
    setAvail(i->k, sizeof(struct sharedSegmentInfo));
    
    return 0; 
}

int sbmem_remove()
{
	if (shm_unlink(name) == -1) 
	{ 
		return -1;
	}
	
	sem_destroy(&(ssi->sem));
	 	
    return 0; 
}

void printExternalFrag()
{
	int size = 0;
    int k;   
    for(k = kMin; k <= kMax; k++)
    {
    	if(getAvail(k) != -1)
    	{
    	    struct info* ptr = otp(getAvail(k));
    	    struct info* cur = ptr;
    	    
    	    do
    	    {
    	  		size = size + (int)pow(2, cur->k) - sizeof(struct info);
    	  		cur = otp(cur->next);
    	    }while(cur != ptr);
    	}
    }
    
    printf("External Fragmentation: %d\n", size);
}

void printInternalFrag()
{
	printf("Internal Fragmentation: %d\n", internalFragmentation);
}
