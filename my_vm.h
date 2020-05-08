#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
//Assume the address space is 48 bits, so the max memory size is 256*1024GB
//Page size is 4KB

//Add any important includes here which you may need

#define PGSIZE 4096
#define TLBSIZE 32

// Maximum size of your memory
//#define MAX_MEMSIZE (uint64_t)1024*(uint64_t)1024*(uint64_t)1024
//1024*1024*1024=1073741824    1024*1024*1024*1024=1099511627776  128G=137438953472  32G=34359738368
//#define MAX_MEMSIZE 8589934592
//#define MAX_MEMSIZE 137438953472
#define MAX_MEMSIZE 1073741824
//#define MAX_MEMSIZE 34359738368

#define LEVELBITS 9

typedef uint64_t address_t;

// address format: pgd(9) pud(9) pmd(9) pte(9) offset(12)

typedef uint64_t pgd_t;
typedef uint64_t pud_t;
typedef uint64_t pmd_t;
typedef uint64_t pte_t;

// Represents a page table entry
//typedef unsigned long pte_t;
//typedef uint64_t pte_t;

// Represents a page directory entry
//typedef unsigned long pde_t;
//typedef uint64_t pde_t;

typedef uint64_t pageno_t;


//Structure to represents TLB
typedef struct tlb{
    //Assume your TLB is a direct mapped TLB of TBL_SIZE (entries). You must also define wth TBL_SIZE in this file.
    //Assume each bucket to be 4 bytes
	bool valid;
	pageno_t key;
	pageno_t value;
}tlb;
tlb _tlb_store[TLBSIZE];

char *memstart;
//pde_t *_pagedir;
uint32_t *pbitmap;
uint32_t *vbitmap;
uint64_t _pagenum;
uint32_t _offsetbits;
uint32_t _tablesize;
uint32_t _tlbmodbits;

void set_bitmap(uint32_t *bitmap, uint64_t k);
void clear_bitmap(uint32_t *bitmap, uint64_t k);
bool get_bitmap(uint32_t *bitmap, uint64_t k);

uint32_t get_pgdindex(address_t va);
uint32_t get_pudindex(address_t va);
uint32_t get_pmdindex(address_t va);
uint32_t get_pteindex(address_t va);

uint32_t get_pageoffset(address_t va);
uint32_t get_pow2(uint64_t number);
pageno_t get_next_avail_pfn();
pageno_t transfer_ppntopfn(pageno_t ppn);
pageno_t transfer_pfntoppn(pageno_t pfn);

pthread_rwlock_t _pagetable_lock;
pthread_rwlock_t _tlb_lock[TLBSIZE];

void set_physical_mem();
address_t translate(address_t va);
void* get_next_avail(uint64_t num_pages);
bool page_map(pageno_t vpn, pageno_t pfn);
void *a_malloc(uint64_t num_bytes);
void a_free(void *va, uint64_t size);
void put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);

void tlb_add(pageno_t vpn, pageno_t pfn);
uint64_t tlb_lookup(pageno_t vpn);
void tlb_freeupdate(pageno_t vpn);

void *umalloc(uint64_t num_bytes);
void ufree(void *va, uint64_t size);
void put_val(void *va, void *val, int size);
void get_val(void *va, void *val, int size);
void p_mat_mult(void *mat1, void *mat2, int size, void *answer);
void hold_rlock(pthread_rwlock_t *lock);
void hold_wlock(pthread_rwlock_t *lock);
void release_lock(pthread_rwlock_t *lock);
address_t p_translate(address_t va);

#endif
