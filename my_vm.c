#include "my_vm.h"

pthread_mutex_t _init_mutex = PTHREAD_MUTEX_INITIALIZER;
bool _init_physical = false;
pgd_t *_pgd = NULL;

void set_physical_mem() {
    //Allocate physical memory using mmap or malloc; this is the total size of your memory you are simulating
	if(posix_memalign(&memstart, PGSIZE, MAX_MEMSIZE) != 0) {
		fprintf(stderr, "posix_memalign error!\n");
		exit(1);
	}
	memset(memstart, 0, MAX_MEMSIZE);
	_offsetbits = get_pow2(PGSIZE);
	_pagenum = MAX_MEMSIZE/PGSIZE;
	_tablesize = 1<<LEVELBITS;
	_tlbmodbits = ~((~0)<<get_pow2(TLBSIZE));
	uint64_t bitmapsize = _pagenum/8;  // the size of bitmap, in terms of byte
    //HINT: Also calculate the number of physical and virtual pages and allocate virtual and physical bitmaps and initialize them
	pbitmap = (uint32_t*)calloc(bitmapsize, 1);
	if(pbitmap == NULL) {
		fprintf(stderr, "calloc for pbitmap fails!\n");
		exit(1);
	}
	vbitmap = (uint32_t*)calloc(bitmapsize, 1);
	if(vbitmap == NULL) {
		fprintf(stderr, "calloc for vbitmap fails!\n");
		exit(1);
	}
	_pgd = (pgd_t*)calloc(_tablesize, sizeof(address_t));
	if(_pgd == NULL) {
		fprintf(stderr, "calloc for _pgd fails!\n");
		exit(1);
	}

	for(int i=0;i<TLBSIZE;++i)	_tlb_store[i].valid = false;

	if(0 != pthread_rwlock_init(&_pagetable_lock, NULL)) {
		fprintf(stderr, "init pagetable lock fails!\n");
		exit(1);
	}
	for(int i=0;i<TLBSIZE;++i) {
		if(0 != pthread_rwlock_init(&_tlb_lock[i], NULL)) {
			fprintf(stderr, "init tlblock[%d] fails!\n", i);
			exit(1);
		}
	}

	_init_physical = true;
}

/*The function takes a virtual address and performs translation to return the physical address*/
address_t translate(address_t va) {
	if(_pgd == NULL) {
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pageno_t vpn = va>>_offsetbits;
	pageno_t tlb_pfn = tlb_lookup(vpn);
	if(tlb_pfn != 0)	return (tlb_pfn<<_offsetbits) | get_pageoffset(va);

	uint32_t pgdindex = get_pgdindex(va);
	if(_pgd[pgdindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pud_t *pudtable = _pgd[pgdindex];
	uint32_t pudindex = get_pudindex(va);
	if(pudtable[pudindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pmd_t *pmdtable = pudtable[pudindex];
	uint32_t pmdindex = get_pmdindex(va);
	if(pmdtable[pmdindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pte_t *ptetable = pmdtable[pmdindex];
	uint32_t pteindex = get_pteindex(va);
	if(ptetable[pteindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}
	pageno_t pfn = ptetable[pteindex];
	tlb_add(vpn, pfn);
	return (pfn<<_offsetbits) | get_pageoffset(va);
}

address_t p_translate(address_t va) {
	if(_pgd == NULL) {
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pageno_t vpn = va>>_offsetbits;
	uint32_t tlbindex = vpn & _tlbmodbits;
	hold_rlock(&_tlb_lock[tlbindex]);
	pageno_t tlb_pfn = tlb_lookup(vpn);
	release_lock(&_tlb_lock[tlbindex]);
	if(tlb_pfn != 0)	return (tlb_pfn<<_offsetbits) | get_pageoffset(va);

	uint32_t pgdindex = get_pgdindex(va);
	if(_pgd[pgdindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pud_t *pudtable = _pgd[pgdindex];
	uint32_t pudindex = get_pudindex(va);
	if(pudtable[pudindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pmd_t *pmdtable = pudtable[pudindex];
	uint32_t pmdindex = get_pmdindex(va);
	if(pmdtable[pmdindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}

	pte_t *ptetable = pmdtable[pmdindex];
	uint32_t pteindex = get_pteindex(va);
	if(ptetable[pteindex] == 0)	{
		fprintf(stderr, "Error! function[%s] line[%d]\n", __func__, __LINE__);
		return 0;
	}
	pageno_t pfn = ptetable[pteindex];
	hold_wlock(&_tlb_lock[tlbindex]);
	tlb_add(vpn, pfn);
	release_lock(&_tlb_lock[tlbindex]);
	return (pfn<<_offsetbits) | get_pageoffset(va);
}

/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
bool page_map(pageno_t vpn, pageno_t pfn) {
	if(_pgd == NULL)	return false;
	uint32_t pgdindex = vpn>>(3*LEVELBITS);
	if(_pgd[pgdindex] == NULL) {
		_pgd[pgdindex] = (pud_t*)calloc(_tablesize, sizeof(address_t));
	}

	pud_t *pudtable = _pgd[pgdindex];
	uint32_t pudindex = (vpn>>(2*LEVELBITS)) & ~((~0)<<LEVELBITS);
	if(pudtable[pudindex] == NULL) {
		pudtable[pudindex] = (pmd_t*)calloc(_tablesize, sizeof(address_t));
	}
	
	pmd_t *pmdtable = pudtable[pudindex];
	uint32_t pmdindex = (vpn>>LEVELBITS) & ~((~0)<<LEVELBITS);
	if(pmdtable[pmdindex] == NULL) {
		pmdtable[pmdindex] = (pte_t*)calloc(_tablesize, sizeof(address_t));
	}

	pte_t *ptetable = pmdtable[pmdindex];
	uint32_t pteindex = vpn & ~((~0)<<LEVELBITS);
	if(ptetable[pteindex] == 0)	{
		ptetable[pteindex] = pfn;
		return true;
	}else	return false;

}

/*Function that gets the next available page */
void *get_next_avail(uint64_t num_pages) {
	if(num_pages==0 || num_pages>_pagenum)	return NULL;
	uint64_t counter, i, j;
	for(i=0;i<_pagenum;++i) {
		if(get_bitmap(vbitmap, i)==0) {
			counter = 1;
			if(counter == num_pages)	break;
			for(j=i+1;j<_pagenum;++j) {
				if(get_bitmap(vbitmap, j)==0)	++counter;
				else	break;
				if(counter==num_pages)	break;
			}
			if(counter==num_pages)	break;
			i = j;
		}
	}
	if(counter != num_pages)	return NULL;
	// i corresponds to the 1st virtual page, and j to the last
	pageno_t vpn, ppn=0, pfn;
	for(vpn=i;vpn<i+num_pages;++vpn) {
		while(get_bitmap(pbitmap, ppn))	++ppn;
		set_bitmap(vbitmap, vpn);
		set_bitmap(pbitmap, ppn);
		pfn = transfer_ppntopfn(ppn);
		if(page_map(vpn, pfn) == false) {
			fprintf(stderr, "page_mmap for vpn=%"PRIu32" pfn=%"PRIu32" fails!\n", vpn, pfn);
			exit(1);
		}
	}
	
	return (void*)(i<<_offsetbits);
}

/* Function responsible for allocating pages and used by the benchmark */
void *a_malloc(uint64_t num_bytes) {
    //HINT: If the physical memory is not yet initialized, then allocate and initialize.
	if(_init_physical == false)	set_physical_mem();
	/* HINT: If the page directory is not initialized, then initialize the page directory. Next, using get_next_avail(), check if there are free pages. If
	free pages are available, set the bitmaps and map a new page. Note, you will have to mark which physical pages are used. */
	if(num_bytes==0 || num_bytes>MAX_MEMSIZE)	return NULL;
	uint64_t num_pages = num_bytes>>_offsetbits;
	if(num_bytes&~((~0)<<_offsetbits))	++num_pages;
	void *malloc_address = get_next_avail(num_pages);
	return malloc_address;
}

void *umalloc(uint64_t num_bytes) {

	if(num_bytes==0 || num_bytes>MAX_MEMSIZE)	return NULL;
	uint64_t num_pages = num_bytes>>_offsetbits;
	if(num_bytes&~((~0)<<_offsetbits))	++num_pages;

	if(0 != pthread_mutex_lock(&_init_mutex)) {
		fprintf(stderr, "pthread_mutex_lock(&_init_mutex) fails!\n");
		exit(1);
	}
	if(_init_physical == false)	set_physical_mem();
	pthread_mutex_unlock(&_init_mutex);

	hold_wlock(&_pagetable_lock);
	void *malloc_address = get_next_avail(num_pages);
	release_lock(&_pagetable_lock);
	return malloc_address;

}

/* Responsible for releasing one or more memory pages using virtual address (va) */
void a_free(void *va, uint64_t size) {
    //Free the page table entries starting from this virtual address (va) Also mark the pages free in the bitmap
    //Only free if the memory from "va" to va+size is valid
	if(size==0 || size>MAX_MEMSIZE)	return;

	uint64_t num_pages = size>>_offsetbits;
	if(size & ~((~0)<<_offsetbits))	++num_pages;

	pageno_t vpn = ((address_t)va)>>_offsetbits;
	bool free_flag = true;
	for(pageno_t i=vpn;i<vpn+num_pages;++i)
		if(get_bitmap(vbitmap, i)==0) {
			free_flag = false;
			break;
		}
	
	if(free_flag) {
		uint32_t pgdindex, pudindex, pmdindex, pteindex;
		pageno_t pfn, ppn;
		for(pageno_t ivpn=vpn;ivpn<vpn+num_pages;++ivpn) {
			pgdindex = ivpn>>(3*LEVELBITS);
			pudindex = (ivpn>>(2*LEVELBITS)) & ~((~0)<<LEVELBITS);
			pmdindex = (ivpn>>LEVELBITS) & ~((~0)<<LEVELBITS);
			pteindex = ivpn & ~((~0)<<LEVELBITS);

			pud_t *pudtable = _pgd[pgdindex];
			pmd_t *pmdtable = pudtable[pudindex];
			pte_t *ptetable = pmdtable[pmdindex];
			pfn = ptetable[pteindex];

			tlb_freeupdate(vpn);
			ptetable[pteindex] = 0;
			// free the ptetable if necessary
			bool freetable_flag = true;
			for(uint32_t i=0;i<_tablesize;++i)
				if(ptetable[i]!=0) {
					freetable_flag = false;
					break;
				}

			if(freetable_flag) {
				free(pmdtable[pmdindex]);
				pmdtable[pmdindex] = 0;

				// free the pmdtable if necessary
				for(uint32_t i=0;i<_tablesize;++i)
					if(pmdtable[i]!=0) {
						freetable_flag = false;
						break;
					}

				if(freetable_flag) {
					free(pudtable[pudindex]);
					pudtable[pudindex] = 0;

					// free the pudtable if necessary
					for(uint32_t i=0;i<_tablesize;++i)
						if(pudtable[i]!=0) {
							freetable_flag = false;
							break;
						}

					if(freetable_flag) {
						free(_pgd[pgdindex]);
						_pgd[pgdindex] = 0;
					}
				}
			}

			ppn = transfer_pfntoppn(pfn);
			clear_bitmap(pbitmap, ppn);
			clear_bitmap(vbitmap, ivpn);
		}
	} // end of free process
}

void ufree(void *va, uint64_t size) {
    //Free the page table entries starting from this virtual address (va) Also mark the pages free in the bitmap
    //Only free if the memory from "va" to va+size is valid
	if(size==0 || size>MAX_MEMSIZE)	return;

	uint64_t num_pages = size>>_offsetbits;
	if(size & ~((~0)<<_offsetbits))	++num_pages;

	pageno_t vpn = ((address_t)va)>>_offsetbits;
	bool free_flag = true;
	hold_wlock(&_pagetable_lock);
	for(pageno_t i=vpn;i<vpn+num_pages;++i)
		if(get_bitmap(vbitmap, i)==0) {
			free_flag = false;
			break;
		}
	
	if(free_flag) {
		uint32_t pgdindex, pudindex, pmdindex, pteindex;
		pageno_t pfn, ppn;
		for(pageno_t ivpn=vpn;ivpn<vpn+num_pages;++ivpn) {
			pgdindex = ivpn>>(3*LEVELBITS);
			pudindex = (ivpn>>(2*LEVELBITS)) & ~((~0)<<LEVELBITS);
			pmdindex = (ivpn>>LEVELBITS) & ~((~0)<<LEVELBITS);
			pteindex = ivpn & ~((~0)<<LEVELBITS);

			pud_t *pudtable = _pgd[pgdindex];
			pmd_t *pmdtable = pudtable[pudindex];
			pte_t *ptetable = pmdtable[pmdindex];
			pfn = ptetable[pteindex];
			
			tlb_freeupdate(vpn);
			ptetable[pteindex] = 0;
			// free the ptetable if necessary
			bool freetable_flag = true;
			for(uint32_t i=0;i<_tablesize;++i)
				if(ptetable[i]!=0) {
					freetable_flag = false;
					break;
				}

			if(freetable_flag) {
				free(pmdtable[pmdindex]);
				pmdtable[pmdindex] = 0;

				// free the pmdtable if necessary
				for(uint32_t i=0;i<_tablesize;++i)
					if(pmdtable[i]!=0) {
						freetable_flag = false;
						break;
					}

				if(freetable_flag) {
					free(pudtable[pudindex]);
					pudtable[pudindex] = 0;

					// free the pudtable if necessary
					for(uint32_t i=0;i<_tablesize;++i)
						if(pudtable[i]!=0) {
							freetable_flag = false;
							break;
						}

					if(freetable_flag) {
						free(_pgd[pgdindex]);
						_pgd[pgdindex] = 0;
					}
				}
			}

			ppn = transfer_pfntoppn(pfn);
			clear_bitmap(pbitmap, ppn);
			clear_bitmap(vbitmap, ivpn);
		}
	} // end of free process
	release_lock(&_pagetable_lock);
}

/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
*/
void put_value(void *va, void *val, int size) {
    /* HINT: Using the virtual address and translate(), find the physical page. Copy
       the contents of "val" to a physical page. NOTE: The "size" value can be larger
       than one page. Therefore, you may have to find multiple pages using translate()
       function.*/
	if(size<=0 || val==NULL)	return;
	pageno_t vpn_start = (address_t)va >> _offsetbits;
	pageno_t vpn_end = ((address_t)va + size-1) >> _offsetbits;
	address_t pa;

	// check the validation first!
	for(pageno_t vpn=vpn_start;vpn<=vpn_end;++vpn)	if(get_bitmap(vbitmap, vpn)==0)	return;

	if(vpn_start == vpn_end) {
		pa = translate((address_t)va);
		if(pa == 0)	return;
		memcpy((void*)pa, val, size);
	}else {
		uint64_t remain = ((vpn_start+1)<<_offsetbits) - (address_t)va;
		pa = translate((address_t)va);
		if(pa == 0)	return;
		memcpy((void*)pa, val, remain);
		val = (void*)((address_t)val + remain);
		size -= remain;
		address_t va_tmp;
		for(pageno_t vpn_mid=vpn_start+1;vpn_mid<vpn_end;++vpn_mid) {
			va_tmp = vpn_mid << _offsetbits;
			pa = translate(va_tmp);
			if(pa == 0)	return;
			memcpy((void*)pa, val, PGSIZE);
			size -= PGSIZE;
			val = (void*)((address_t)val + PGSIZE);
		}

		pa = translate((address_t)(vpn_end<<_offsetbits));
		if(pa == 0)	return;
		memcpy((void*)pa, val, size);
	}
}

void put_val(void *va, void *val, int size) {
    /* HINT: Using the virtual address and translate(), find the physical page. Copy
       the contents of "val" to a physical page. NOTE: The "size" value can be larger
       than one page. Therefore, you may have to find multiple pages using translate()
       function.*/
	if(size<=0 || val==NULL)	return;
	pageno_t vpn_start = (address_t)va >> _offsetbits;
	pageno_t vpn_end = ((address_t)va + size-1) >> _offsetbits;
	address_t pa;

	hold_rlock(&_pagetable_lock);
	// check the validation first!
	for(pageno_t vpn=vpn_start;vpn<=vpn_end;++vpn)	
		if(get_bitmap(vbitmap, vpn)==0)	{
			release_lock(&_pagetable_lock);
			return;
		}

	if(vpn_start == vpn_end) {
		pa = p_translate((address_t)va);
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy((void*)pa, val, size);
	}else {
		uint64_t remain = ((vpn_start+1)<<_offsetbits) - (address_t)va;
		pa = p_translate((address_t)va);
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy((void*)pa, val, remain);
		val = (void*)((address_t)val + remain);
		size -= remain;
		address_t va_tmp;
		for(pageno_t vpn_mid=vpn_start+1;vpn_mid<vpn_end;++vpn_mid) {
			va_tmp = vpn_mid << _offsetbits;
			pa = p_translate(va_tmp);
			if(pa == 0)	{
				release_lock(&_pagetable_lock);
				return;
			}
			memcpy((void*)pa, val, PGSIZE);
			size -= PGSIZE;
			val = (void*)((address_t)val + PGSIZE);
		}

		pa = p_translate((address_t)(vpn_end<<_offsetbits));
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy((void*)pa, val, size);
	}
	release_lock(&_pagetable_lock);
}

/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {
    /* HINT: put the values pointed to by "va" inside the physical memory at given
    "val" address. Assume you can access "val" directly by derefencing them.
    If you are implementing TLB,  always check first the presence of translation in TLB before proceeding forward */
	if(size<=0 || val==NULL)	return;
	pageno_t vpn_start = (address_t)va >> _offsetbits;
	pageno_t vpn_end = ((address_t)va+size-1) >> _offsetbits;
	address_t pa;
	if(vpn_start == vpn_end) {
		pa = translate((address_t)va);
		if(pa == 0)	return;
		memcpy(val, (void*)pa, size);
	}else {
		uint32_t remain = ((vpn_start+1)<<_offsetbits) - (address_t)va;
		pa = translate((address_t)va);
		if(pa == 0)	return;
		memcpy(val, (void*)pa, remain);
		val = (void*)((address_t)val + remain);
		size -= remain;
		address_t va_tmp;
		for(pageno_t vpn_mid=vpn_start+1;vpn_mid<vpn_end;++vpn_mid) {
			va_tmp = vpn_mid << _offsetbits;
			pa = translate(va_tmp);
			if(pa == 0)	return;
			memcpy(val, (void*)pa, PGSIZE);
			size -= PGSIZE;
			val = (void*)((address_t)val + PGSIZE);
		}

		pa = translate((address_t)(vpn_end<<_offsetbits));
		if(pa == 0)	return;
		memcpy(val, (void*)pa, size);
	}
}

void get_val(void *va, void *val, int size) {
    /* HINT: put the values pointed to by "va" inside the physical memory at given
    "val" address. Assume you can access "val" directly by derefencing them.
    If you are implementing TLB,  always check first the presence of translation in TLB before proceeding forward */
	if(size<=0 || val==NULL)	return;
	pageno_t vpn_start = (address_t)va >> _offsetbits;
	pageno_t vpn_end = ((address_t)va+size-1) >> _offsetbits;
	address_t pa;
	hold_rlock(&_pagetable_lock);
	if(vpn_start == vpn_end) {
		pa = p_translate((address_t)va);
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy(val, (void*)pa, size);
	}else {
		uint32_t remain = ((vpn_start+1)<<_offsetbits) - (address_t)va;
		pa = p_translate((address_t)va);
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy(val, (void*)pa, remain);
		val = (void*)((address_t)val + remain);
		size -= remain;
		address_t va_tmp;
		for(pageno_t vpn_mid=vpn_start+1;vpn_mid<vpn_end;++vpn_mid) {
			va_tmp = vpn_mid << _offsetbits;
			pa = translate(va_tmp);
			if(pa == 0)	{
				release_lock(&_pagetable_lock);
				return;
			}
			memcpy(val, (void*)pa, PGSIZE);
			size -= PGSIZE;
			val = (void*)((address_t)val + PGSIZE);
		}

		pa = translate((address_t)(vpn_end<<_offsetbits));
		if(pa == 0)	{
			release_lock(&_pagetable_lock);
			return;
		}
		memcpy(val, (void*)pa, size);
	}
	release_lock(&_pagetable_lock);
}

/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
    matrix accessed. Similar to the code in test.c, you will use get_value() to
    load each element and perform multiplication. Take a look at test.c! In addition to 
    getting the values from two matrices, you will perform multiplication and store the result to the "answer array"*/
	address_t address_m1, address_m2, address_ans;
	int tmp, tmp1, tmp2;
	for(uint64_t i=0;i<size;++i)
		for(uint64_t j=0;j<size;++j) {
			tmp = 0;
			for(uint64_t k=0;k<size;++k) {
				address_m1 = (address_t)mat1 + (i*size+k)*sizeof(int);
				address_m2 = (address_t)mat2 + (k*size+j)*sizeof(int);
				get_value((void*)address_m1, &tmp1, sizeof(int));
				get_value((void*)address_m2, &tmp2, sizeof(int));
				tmp += tmp1*tmp2;
			}
			address_ans = (address_t)answer + (i*size+j)*sizeof(int);
			put_value((void*)address_ans, &tmp, sizeof(int));
		}
	
}

void p_mat_mult(void *mat1, void *mat2, int size, void *answer) {
	address_t address_m1, address_m2, address_ans;
	int tmp, tmp1, tmp2;
	for(uint64_t i=0;i<size;++i)
		for(uint64_t j=0;j<size;++j) {
			tmp = 0;
			for(uint64_t k=0;k<size;++k) {
				address_m1 = (address_t)mat1 + (i*size+k)*sizeof(int);
				address_m2 = (address_t)mat2 + (k*size+j)*sizeof(int);
				get_val((void*)address_m1, &tmp1, sizeof(int));
				get_val((void*)address_m2, &tmp2, sizeof(int));
				tmp += tmp1*tmp2;
			}
			address_ans = (address_t)answer + (i*size+j)*sizeof(int);
			put_val((void*)address_ans, &tmp, sizeof(int));
		}
	
}

void set_bitmap(uint32_t *bitmap, uint64_t k) {
	bitmap[k>>5] |= (1<<(k&(~((~0)<<5))));
}

void clear_bitmap(uint32_t *bitmap, uint64_t k) {
	bitmap[k>>5] &= ~(1<<(k&(~((~0)<<5))));
}

bool get_bitmap(uint32_t *bitmap, uint64_t k) {
	if((bitmap[k>>5]>>(k&~((~0)<<5))) & 1)	return true;
	else	return false;
}

uint32_t get_pageoffset(address_t va) {
	return va & ~((~0)<<_offsetbits);
}

uint32_t get_pgdindex(address_t va) {
	return va >> (_offsetbits + 3*LEVELBITS);
}

uint32_t get_pudindex(address_t va) {
	return (va >> (_offsetbits + 2*LEVELBITS)) & ~((~0)<<LEVELBITS);
}

uint32_t get_pmdindex(address_t va) {
	return (va >> (_offsetbits + LEVELBITS)) & ~((~0)<<LEVELBITS);
}

uint32_t get_pteindex(address_t va) {
	return (va>>_offsetbits) & ~((~0)<<LEVELBITS);
}

uint32_t get_pow2(uint64_t number) {
	uint32_t counter = 0;
	number >>= 1;
	while(number) {
		++counter;
		number >>= 1;
	}
	return counter;
}

pageno_t transfer_ppntopfn(pageno_t ppn) {
	return (((address_t)memstart)>>_offsetbits) + ppn;
}

pageno_t transfer_pfntoppn(pageno_t pfn) {
	return pfn - ((address_t)memstart>>_offsetbits);
}

uint64_t tlb_lookup(pageno_t vpn) {
	uint32_t target = vpn & _tlbmodbits;
	if(_tlb_store[target].key==vpn && _tlb_store[target].valid==true)	return _tlb_store[target].value;
	return 0;
}

void tlb_add(pageno_t vpn, pageno_t pfn) {
	uint32_t target = vpn & _tlbmodbits;
	_tlb_store[target].key = vpn;
	_tlb_store[target].value = pfn;
	_tlb_store[target].valid = true;
}

void tlb_freeupdate(pageno_t vpn) {
	uint32_t target = vpn & _tlbmodbits;
	if(_tlb_store[target].key==vpn && _tlb_store[target].valid==true)	_tlb_store[target].valid = false;
}

void release_lock(pthread_rwlock_t *lock) {
	if(0 != pthread_rwlock_unlock(lock)) {
		fprintf(stderr, "pthread_rwlock_unlock(lock) fails!\n");
		exit(1);
	}
}

void hold_rlock(pthread_rwlock_t *lock) {
	if(0 != pthread_rwlock_rdlock(lock)) {
		fprintf(stderr, "pthread_rwlock_rdlock(lock) fails!\n");
		exit(1);
	}
}

void hold_wlock(pthread_rwlock_t *lock) {
	if(0 != pthread_rwlock_wrlock(lock)) {
		fprintf(stderr, "pthread_rwlock_wrlock(lock) fails!\n");
		exit(1);
	}
}

