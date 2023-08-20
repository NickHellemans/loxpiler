#pragma once

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_MAX 256


typedef struct {
	Chunk* chunk;
	//instruction pointer, stores location of instruction to be executed next
	//Sometimes called program counter (pc)
	uint8_t* ip;
	Value stack[STACK_MAX];
	//Store pointer instead of index
	//It is faster to deref a pointer than to index in an array
	//Same reason we store ip as a pointer
	//Points at first empty element in stack
	Value* stackTop;
	//Interned strings
	Table strings;
	//All allocated objects to track
	Obj* objects;

} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void init_vm(void);
void free_vm(void);
InterpretResult interpret(const char* source);
void push_stack(Value value);
Value pop_stack(void);