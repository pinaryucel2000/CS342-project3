int sbmem_init(int segmentsize); 
int sbmem_remove(); 

int sbmem_open();

void *sbmem_alloc (int size);
void sbmem_free(void *p);
int sbmem_close(); 

int getAvail(int index);
struct info* findBuddy(struct info* block);
void printp(struct info* ptr);

void setAvail(int index, int newValue);
int isAvail(struct info* block);
void removeFromAvail(struct info* block);
void setAsAvail(struct info* block);
struct info* otp(int offset);
int pto(struct info* ptr);
void printExternalFrag();
void printInternalFrag();
void printAddress(void* ptr);
