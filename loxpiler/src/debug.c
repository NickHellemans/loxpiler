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

	if(offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
		printf("   | ");
	} else {
		printf("%4d ", chunk->lines[offset]);
	}

	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
		case OP_CONSTANT:
			return constant_instruction("OP_CONSTANT", chunk, offset);
		case OP_NIL:
			return simple_instruction("OP_NIL", offset);
		case OP_TRUE:
			return simple_instruction("OP_TRUE", offset);
		case OP_FALSE:
			return simple_instruction("OP_FALSE", offset);
		case OP_EQUAL:
			return simple_instruction("OP_EQUAL", offset);
		case OP_GREATER:
			return simple_instruction("OP_GREATER", offset);
		case OP_LESS:
			return simple_instruction("OP_LESS", offset);
		case OP_ADD:
			return simple_instruction("OP_ADD", offset);
		case OP_SUBTRACT:
			return simple_instruction("OP_SUBTRACT", offset);
		case OP_MULTIPLY:
			return simple_instruction("OP_MULTIPLY", offset);
		case OP_DIVIDE:
			return simple_instruction("OP_DIVIDE", offset);
		case OP_NOT:
			return simple_instruction("OP_NOT", offset);
		case OP_NEGATE:
			return simple_instruction("OP_NEGATE", offset);
		case OP_RETURN:
			return simple_instruction("OP_RETURN", offset);
			
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}
}