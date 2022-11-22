#include "stringid.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define STRING_MAXPAGE 256
#define STRING_SECTION 14

#define EOS_PADDING 0xff
#define EOS_FREE 0xfe

// sizeof stringid_page == STRINGID_PAGESIZE (1M)

struct stringid_page {
	unsigned short header[0x10000];
	unsigned char data[0x10000][STRING_SECTION];
};

struct stringid_index {
	int freeslot;
	int freelist;
	struct stringid_page *p;
};

struct stringpool {
	int pages;
	struct stringid_index p[STRING_MAXPAGE];
};

static inline uint8_t
padding_tag(struct stringid_page *p, int sec) {
	return p->data[sec][STRING_SECTION-1];
}

static inline void
padding_free(struct stringid_page *p, int sec) {
	p->data[sec][STRING_SECTION-1] = EOS_FREE;
}

static inline uint16_t
get_number(struct stringid_page *p, int sec) {
	return (uint16_t)(p->data[sec][0] | p->data[sec][1] << 8);
}

static inline void
set_number(struct stringid_page *p, int sec, uint16_t v) {
	p->data[sec][0] = v & 0xff;
	p->data[sec][1] = (v >> 8) & 0xff;
}

static void
page_init(struct stringid_index *p) {
	struct stringid_page *tmp = p->p;
	int i;
	for (i=0;i<0x10000-1;i++) {
		tmp->header[i] = i + 1;
	}
	tmp->header[0xffff] = 0xffff;
	padding_free(tmp, 0xffff);
	p->freeslot = 0x10000;
	p->freelist = 0;
}

static void
new_page(struct stringpool *S, int page) {
	assert(page >= 0 && page < STRING_MAXPAGE);
	assert(S->p[page].p == NULL);
	S->p[page].p = (struct stringid_page *)malloc(sizeof(struct stringid_page));
	page_init(&S->p[page]);
	if (page+1 > S->pages)
		S->pages = page+1;
}

struct stringpool *
stringid_newpool() {
	struct stringpool *S = (struct stringpool *)malloc(sizeof(*S));
	memset(S, 0, sizeof(*S));
	return S;
}

void
stringid_deletepool(struct stringpool *S) {
	int i;
	for (i=0;i<STRING_MAXPAGE;i++) {
		free(S->p[i].p);
	}
	free(S);
}

static int
string_len(struct stringid_page *p, int sec) {
	int len = STRING_SECTION - 3;
	while (p->header[sec] != sec) {
		len += STRING_SECTION;
		sec = p->header[sec];
	}
	switch (padding_tag(p, sec)) {
	case 0:
		return len;
	case EOS_PADDING:
		break;
	default:
		return -1;
	}
	int i;
	for (i=STRING_SECTION-2;i>0;i--) {
		if (p->data[sec][i] == 0)
			break;
	}
	len -= STRING_SECTION-1-i;
	return len;
}

static int
string_eq(struct stringid_page *p, int sec, const char *str, int l) {
	if (l <= STRING_SECTION-2) {
		return memcmp(str, &p->data[sec][2], l) == 0;
	}
	if (memcmp(str, &p->data[sec][2], STRING_SECTION-2) != 0)
		return 0;
	l-=STRING_SECTION-2;
	str+=STRING_SECTION-2;
	while ((sec = p->header[sec]), l >= STRING_SECTION) {
		if (memcmp(str, p->data[sec], STRING_SECTION) != 0)
			return 0;
		l-=STRING_SECTION;
		str+=STRING_SECTION;
	}
	return memcmp(str, p->data[sec], l) != 0;
}

static void
string_cp(struct stringid_page *p, int sec, char *str, int l) {
	if (l <= STRING_SECTION-2) {
		memcpy(str, &p->data[sec][2], l);
		return;
	}
	memcpy(str, &p->data[sec][2], STRING_SECTION-2);
	l-=STRING_SECTION-2;
	str+=STRING_SECTION-2;
	while ((sec = p->header[sec]), l >= STRING_SECTION) {
		memcpy(str, p->data[sec], STRING_SECTION);
		l-=STRING_SECTION;
		str+=STRING_SECTION;
	}
	memcpy(str, p->data[sec], l);
}

int
stringid_eq(struct stringpool *S, stringid_t id, const char *str, int sz) {
	int page = id.idx >> 16;
	int sec = id.idx & 0xffff;
	assert(page < S->pages);
	struct stringid_page *pp = S->p[page].p;
	if (pp) {
		return string_len(pp, sec) == sz && string_eq(pp, sec, str, sz);
	}
	return 0;
}

static inline int
string_continuous(struct stringid_page *S, int sec) {
	for (;;) {
		int next = S->header[sec];
		if (next == sec)
			return 1;
		if (next != sec+1)
			return 0;
		sec = next;
	}
}

const char *
stringid_str(struct stringpool *S, stringid_t id, char *buffer, int *sz) {
	int bufsz = *sz;
	int page = id.idx >> 16;
	int sec = id.idx & 0xffff;
	assert(page < S->pages);
	struct stringid_page *pp = S->p[page].p;
	assert(pp != NULL);
	*sz = string_len(pp, sec);
	if (string_continuous(pp, sec))
		return (const char *)&pp->data[sec][2];
	if (bufsz > *sz) {
		string_cp(pp, sec, buffer, *sz);
		buffer[*sz] = 0;
	} else if (bufsz > 0) {
		string_cp(pp, sec, buffer, bufsz-1);
		buffer[bufsz-1] = 0;
	}
	return buffer;
}

static inline int
count_slots(int sz) {
	return (sz + 3) / STRING_SECTION;
}

static int
find_page(struct stringpool *S, int n) {
	int i;
	struct stringid_page *pp = NULL;
	for (i=S->pages-1;i>=0;i--) {
		pp = S->p[i].p;
		if (pp == NULL) {
			new_page(S, i);
			pp = S->p[i].p;
			break;
		}
		if (S->p[i].freeslot >= n)
			break;
	}
	if (pp == NULL) {
		i = S->pages;
		new_page(S, i);
		pp = S->p[i].p;
	}
	return i;
}

stringid_t
stringid_create(struct stringpool *S, const char *str, int sz) {
	int page = find_page(S, count_slots(sz));
	int sec = S->p[page].freelist;
	struct stringid_index *idx = &S->p[page];
	struct stringid_page *pp = idx->p;
	stringid_t id = { page << 16 | sec }; 
	pp->data[sec][0] = 0;
	pp->data[sec][1] = 0;
	if (sz <= STRING_SECTION - 3) {
		memcpy(&pp->data[sec][2], str, sz);
		pp->data[sec][2+sz] = 0;
		idx->freelist = pp->header[sec];
		pp->header[sec] = sec;
		--idx->freeslot;
		return id;
	}
	memcpy(&pp->data[sec][2], str, STRING_SECTION - 2);
	str += STRING_SECTION - 2;
	sz -= STRING_SECTION - 2;
	int s = 1;
	for (;;) {
		sec = pp->header[sec];
		++s;
		if (sz < STRING_SECTION) {
			memcpy(pp->data[sec], str, sz);
			pp->data[sec][sz] = 0;
			int i;
			for (i=sz+1;i<STRING_SECTION;i++) {
				pp->data[sec][i] = EOS_PADDING;
			}
			idx->freelist = pp->header[sec];
			pp->header[sec] = sec;
			idx->freeslot -= s;
			return id;
		}
		memcpy(pp->data[sec], str, STRING_SECTION);
		str += STRING_SECTION;
		sz -= STRING_SECTION;
	}
}

void
stringid_release(struct stringpool *S, stringid_t id) {
	int page = id.idx >> 16;
	int sec = id.idx & 0xffff;
	assert(page < S->pages);
	struct stringid_index * idx = &S->p[page];
	int count = get_number(idx->p, sec);
	if (count == 0) {
		int n = 1;
		int freelist = idx->freelist;
		idx->freelist = sec;
		// count list, and find last slot
		while (idx->p->header[sec] != sec) {
			sec = idx->p->header[sec];
			++n;
		}
		if (idx->freeslot == 0) {
			padding_free(idx->p, sec);
		} else {
			idx->p->header[sec] = freelist;
		}
		idx->freeslot += n;
	} else {
		set_number(idx->p, sec, count-1);
	}
}

static int
count_slots_in_header(struct stringid_page *p, int sec) {
	int n = 1;
	while (p->header[sec] != sec) {
		++n;
		sec = p->header[sec];
	}
	return n;
}

stringid_t
stringid_clone(struct stringpool *S, stringid_t id) {
	int page = id.idx >> 16;
	int sec = id.idx & 0xffff;
	assert(page < S->pages);
	struct stringid_index * idx = &S->p[page];
	int count = get_number(idx->p, sec);
	if (count < 0xffff) {
		set_number(idx->p, sec, count + 1);
		return id;
	}
	int n = count_slots_in_header(idx->p, sec);
	int dpage = find_page(S, n);
	int dsec = S->p[dpage].freelist;
	struct stringid_index *didx = &S->p[dpage];
	stringid_t rid = { dpage << 16 | dsec };
	unsigned char *refcount = didx->p->data[dsec]; 
	int i;
	struct stringid_page *sp = idx->p;
	struct stringid_page *dp = didx->p;
	int lastsec = dsec;
	for (i=0;i<n;i++) {
		memcpy(dp->data[dsec], sp->data[sec], STRING_SECTION);
		lastsec = dsec;
		dsec = dp->header[dsec];
		sec = sp->header[sec]; 
	}
	dp->header[lastsec] = lastsec;
	didx->freelist = dsec;
	didx->freeslot -= n;
	refcount[0] = 0;
	refcount[1] = 0;
	return rid;
}

static void
dump_list(struct stringid_page *p, int sec, int freelist) {
	printf("[%d] ", sec);
	int count = get_number(p, sec);
	while (sec != p->header[sec]) {
		sec = p->header[sec];
		if (!freelist) 
			printf("%d ", sec);
	}
	switch (padding_tag(p, sec)) {
	case 0:
	case EOS_PADDING:
		printf("(%d)", count);
		break;
	case EOS_FREE:
		printf("FREE");
		break;
	default:
		printf("INVALID");
		break;
	}
	printf("\n");
}

static void
dump_page(struct stringpool *S, int page) {
	struct stringid_index *idx = &S->p[page];
	printf("freeslot = %d, freelist = %d\n", idx->freeslot, idx->freelist);
	char mark[0x10000];
	memset(mark, 0, sizeof(mark));
	int i;
	for (i=0;i<0x10000;i++) {
		if (idx->p->header[i]!=i)
			mark[idx->p->header[i]] = 1;
	}
	
	for (i=0;i<0x10000;i++) {
		if (!mark[i]) {
			dump_list(idx->p, i, idx->freelist == i);
			if (i != idx->freelist) {
				char tmp[128];
				int sz = sizeof(tmp);
				stringid_t id = { page << 16 | i };
				const char * s = stringid_str(S,  id, tmp, &sz);
				printf("(%d) %s\n", sz, s);
			}
		}
	}
}

void
stringid_dump(struct stringpool *S) {
	printf("pages = %d\n", S->pages);
	int i;
	for (i=0;i<S->pages;i++) {
		printf("Page [%d]\n", i);
		dump_page(S, i);
	}
}

int
main() {
	struct stringpool *S = stringid_newpool();
	stringid_t sid = stringid_literal(S, "Hello World");
	char tmp[128];
	int sz = sizeof(tmp);
	printf("%s\n", stringid_str(S, sid, tmp, &sz));
	int i;
	for (i=0;i<0x20000;i++) {
		sid = stringid_clone(S, sid);
	}
	stringid_dump(S);
	stringid_deletepool(S);
	return 0;
}