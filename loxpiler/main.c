// loxpiler.c : This file contains the 'main' function. Program execution begins and ends there.
//

#include <stdio.h>
#include <stdlib.h>
#include "./src/common.h"
#include "src/chunk.h"
#include "src/debug.h"
#include "src/vm.h"
#include <string.h>

static void repl(void) {
	char line[1024];

	for(;;) {
		printf(">> ");
		if(!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		interpret(line);
	}
}

//How much to allocate for buffer? size of file in unknown
//Open file --> seek to end with fseek --> use ftell to tell us how many bytes we are away from start
//We are at end end so == size
//Rewind back to begin, allocate a string of that size and read file into it
static char* read_file(const char* path) {
	FILE* file;
	errno_t err = fopen_s(&file, path, "rb");

	if(file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);

	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';
	fclose(file);
	return buffer;
}

static void run_file(const char* path) {
	char* source = read_file(path);
	InterpretResult result = interpret(source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR)
		exit(65);
	if (result == INTERPRET_RUNTIME_ERROR)
		exit(70);
}

int main(int argc, const char* argv[]) {
	init_vm();
	
	if(argc == 1) {
		repl();
	} else if (argc == 2) {
		run_file(argv[1]);
	} else {
		fprintf(stderr, "Usage: clox [path]\n");
		exit(64);
	}

	free_vm();
	return 0;
}
