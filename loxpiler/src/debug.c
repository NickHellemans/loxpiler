#include "debug.h"
#include "value.h"
#include <stdio.h>

void disassemble_chunk(Chunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	//Instructions have different sizes
	//Let fn return offset of next instruction
	for(int offset = 0; offset < chunk->size; ) {
		offset = disassemble_instruction(chunk, offset);
	}
}

static int constant_instruction(const char* name, Chunk* chunk, int offset) {

	//Get index of constant out of bytecode stored next to opcode
	uint8_t constantIdx = chunk->code[offset + 1];
	printf("%-16s %4d '", name, constantIdx);
	//Print constant (known at compile time)
	print_value(chunk->constants.values[constantIdx]);
	printf("'\n");
	//Constant consists out of 2 bytes so add 2 to offset to get to next instruction
	return offset + 2;
}

static int simple_instruction(const char* name, int offset) {
	printf("%s\n", name);
	return offset + 1;
}

int disassemble_instruction(Chunk* chunk, int offset) {
	printf("%04d ", offset);

	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
		case OP_CONSTANT:
			return constant_instruction("OP_CONSTANT", chunk, offset);
		case OP_RETURN:
			return simple_instruction("OP_RETURN", offset);
			
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}
}