#pragma once
#include "common.h"
#include "value.h"

//Operation code
typedef enum {
	OP_CONSTANT,
	OP_RETURN,
} OpCode;

typedef struct {
	//Array of bytes (instructions)
	int size;
	int capacity;
	uint8_t* code;
	//Store line numbers parallel with bytecode for errors
	int* lines;
	//Constant pool to store every constant
	ValueArray constants;
} Chunk;

void init_chunk(Chunk* chunk);
void write_chunk(Chunk* chunk, uint8_t byte, int line);
int add_constant(Chunk* chunk, Value value);
void free_chunk(Chunk* chunk);
