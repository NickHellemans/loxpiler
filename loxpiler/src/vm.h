#pragma once

#include "chunk.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

//Each function invocation tracks where locals begin + where caller should return after fn
typedef struct {
	//Fn being called (look up constants)
	ObjClosure* closure;
	//Store caller's ip --> return from fn - VM jumps to ip of caller's callframe and resume
	uint8_t* ip;
	//First slot fn can use in VM value stack
	Value* slots;
} CallFrame;

typedef struct {
	CallFrame frames[FRAMES_MAX];
	int frameCount;
	//instruction pointer, stores location of instruction to be executed next
	//Sometimes called program counter (pc)
	//uint8_t* ip;

	Value stack[STACK_MAX];
	//Store pointer instead of index
	//It is faster to deref a pointer than to index in an array
	//Same reason we store ip as a pointer
	//Points at first empty element in stack
	Value* stackTop;
	//Global env
	Table globals;
	//Interned strings
	Table strings;
	//Open upvalues still on stack
	ObjUpvalue* openUpvalues;
	//Live memory
	size_t bytesAllocated;
	//Threshold to trigger gc
	size_t nextGC;
	//All allocated objects to track
	Obj* objects;
	//Mark objects gray that still need to mark potential references
	int grayCount;
	int grayCapacity;
	Obj** grayStack;

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