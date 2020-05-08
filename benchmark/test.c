#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "../my_vm.h"

#define SIZE 100

int main() {
    printf("Allocating three arrays of 40000 bytes\n");
    void *a = a_malloc(10000*4);
    uint64_t old_a = (uint64_t)a;
    void *b = a_malloc(10000*4);
    void *c = a_malloc(10000*4);
    int x0=1, x1=2;
    int y, z;
    int i =0, j=0;
    uint64_t address_a = 0, address_b = 0, address_c = 0;

    printf("Addresses of the allocations: %x, %x, %x\n", (uint64_t)a, (uint64_t)b, (uint64_t)c);
    printf("Storing integers to generate a SIZExSIZE matrix\n");
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (uint64_t)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (uint64_t)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_a, &x0, sizeof(int));
            put_value((void *)address_b, &x1, sizeof(int));
        }
    } 
    printf("Fetching matrix elements stored in the arrays\n");

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (uint64_t)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (uint64_t)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &y, sizeof(int));
            get_value( (void *)address_b, &z, sizeof(int));
            //printf("y=%d ", y);
			//printf("z=%d ", z);
        }
        //printf("\n");
    }
    printf("Performing matrix multiplication with itself!\n");
    mat_mult(a, b, SIZE, c);

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_c = (uint64_t)c + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_c, &y, sizeof(int));
            printf("%d ", y);
        }
        printf("\n");
    }
    printf("Freeing the allocations!\n");
    a_free(a, 10000*4);
    a_free(b, 10000*4);
    a_free(c, 10000*4);
    
    printf("Checking if allocations were freed!\n");
    a = a_malloc(10000*4);
    if ((uint64_t)a == old_a) {
        printf("free function works\n");
    }else {
        printf("free function does not work\n");
		printf("old_a=%x nowa=%x\n", old_a, a);
	}

    return 0;
}
