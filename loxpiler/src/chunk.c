#include "chunk.h"
#include <stdlib.h>
#include "memory.h"

void init_chunk(Chunk* chunk) {
	chunk->size = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
}

void write_chunk(Chunk* chunk, uint8_t byte) {
	if(chunk->size >= chunk->capacity) {
		const int oldCap = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCap);
		chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCap, chunk->capacity);
	}

	chunk->code[chunk->size] = byte;
	chunk->size++;
}

void free_chunk(Chunk* chunk) {
	//Deallocate memory
	FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	//Reset to empty state
	init_chunk(chunk);
}