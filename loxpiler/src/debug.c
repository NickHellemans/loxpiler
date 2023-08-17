#include "debug.h"

#include <stdio.h>

void disassemble_chunk(Chunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	//Instructions have different sizes
	//Let fn return offset of next instruction
	for(int offset = 0; offset < chunk->size; ) {
		offset = disassemble_instruction(chunk, offset);
	}
}

static int simple_instruction(const char* name, int offset) {
	printf("%s\n", name);
	return offset + 1;
}

int disassemble_instruction(Chunk* chunk, int offset) {
	printf("%04d ", offset);

	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
		case OP_RETURN:
			return simple_instruction("OP_RETURN", offset);
			
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}
}