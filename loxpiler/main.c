// loxpiler.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include "./src/common.h"
#include "src/chunk.h"
#include "src/debug.h"
#include "src/vm.h"

int main(int argc, const char* argv[]) {
	init_vm();
	Chunk chunk;
	init_chunk(&chunk);
	int constant = add_constant(&chunk, 1.2);
	write_chunk(&chunk, OP_CONSTANT, 123);
	write_chunk(&chunk, constant, 123);
	write_chunk(&chunk, OP_RETURN,123);
	disassemble_chunk(&chunk, "test chunk");
	interpret(&chunk);
	free_vm();
	free_chunk(&chunk);
	return 0;
}
