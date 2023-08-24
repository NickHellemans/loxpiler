#include "chunk.h"
#include <stdlib.h>
#include "memory.h"
#include "vm.h"

void init_chunk(Chunk* chunk) {
	chunk->size = 0;
	chunk->capacity = 0;
	chunk->lines = NULL;
	chunk->code = NULL;
	init_value_array(&chunk->constants);
}

void write_chunk(Chunk* chunk, uint8_t byte, int line) {
	if(chunk->size >= chunk->capacity) {
		const int oldCap = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCap);
		chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCap, chunk->capacity);
		chunk->lines = GROW_ARRAY(int, chunk->lines, oldCap, chunk->capacity);
	}

	chunk->code[chunk->size] = byte;
	chunk->lines[chunk->size] = line;
	chunk->size++;
}

//returns index where constant was added so we can locate it later
int add_constant(Chunk* chunk, Value value) {
	//Push value on stack so GC does not collect it
	push_stack(value);
	write_value_array(&chunk->constants, value);
	pop_stack();
	return chunk->constants.size - 1;
}

void free_chunk(Chunk* chunk) {
	//Deallocate memory
	FREE_ARRAY(int, chunk->lines, chunk->capacity);
	FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	free_value_array(&chunk->constants);
	//Reset to empty state
	init_chunk(chunk);
}