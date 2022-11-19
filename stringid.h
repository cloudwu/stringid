#ifndef STRING_ID_H
#define STRING_ID_H

#define STRINGID_PAGESIZE 0x100000

typedef struct { unsigned int idx; } stringid_t;

struct stringpool;

struct stringpool * stringid_newpool();
//void stringid_deletepool(struct stringpool *);
//int stringid_setpage(struct stringpool *, int pageid, const char page[STRINGID_PAGESIZE]);	// 0: succ
//const char * stringid_getpage(struct stringpool *, int pageid);
//void stringid_rebuild(struct stringpool *);
void stringid_dump(struct stringpool *);	// for debug

stringid_t stringid_create(struct stringpool *, const char *str, int sz);
#define stringid_literal(S, str) stringid_create(S, str "", sizeof(str) -1)
stringid_t stringid_clone(struct stringpool *, stringid_t id);
void stringid_release(struct stringpool *, stringid_t id);
int stringid_eq(struct stringpool *p, stringid_t id, const char *str, int sz);
const char * stringid_str(struct stringpool *, stringid_t id, char *buffer, int *sz);

#endif
