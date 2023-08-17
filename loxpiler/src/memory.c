#include "memory.h"
#include <stdlib.h>


void* reallocate(void* ptr, size_t oldCap, size_t newCap) {

	if(newCap == 0) {
		free(ptr);
		return NULL;
	}

	void* result = realloc(ptr, newCap);

	if (result == NULL) 
		exit(1);

	return result;
}
