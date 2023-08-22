#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "value.h"

//Better to pass around VM with a pointer, but we use 1 global VM to make code a bit lighter
//We only need one anyways
VM vm;

static void reset_stack(void) {
	//Point at start of array
	vm.stackTop = vm.stack;
}

static void runtime_error(const char* format, ...) {
	va_list args;
	va_start(args, format);

	vfprintf(stderr, format, args);

	va_end(args);
	fputs("\n", stderr);

	size_t instruction = vm.ip - vm.chunk->code - 1;
	int line = vm.chunk->lines[instruction];
	fprintf(stderr, "[line %d] in script\n", line);
	reset_stack();
}

void init_vm(void) {
	reset_stack();
	vm.objects = NULL;
	init_table(&vm.globals);
	init_table(&vm.strings);
}

void free_vm(void) {
	free_table(&vm.globals);
	free_table(&vm.strings);
	free_objects();
}

void push_stack(Value value) {
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop_stack(void) {
	//No need to explicitly remove the value
	//Just decreasing top is enough
	vm.stackTop--;
	return *vm.stackTop;
}

static Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static bool is_falsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
	ObjString* b = AS_STRING(pop_stack());
	ObjString* a = AS_STRING(pop_stack());

	int length = b->length + a->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';
	ObjString* result = take_string(chars, length);

	push_stack(OBJ_VAL(result));
}

//Beating heart of the VM
static InterpretResult run(void) {
#define READ_BYTE()(*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
	//Do while is a trick to make sure every statement is in same scope
	//And can use a semicolon at end
	#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtime_error("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop_stack()); \
      double a = AS_NUMBER(pop_stack()); \
      push_stack(valueType(a op b)); \
    } while (false)

	for(;;) {

#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
			printf("[ ");
			print_value(*slot);
			printf(" ] ");
		}
		printf("TOP\n");
		disassemble_instruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		uint8_t instruction;

		//Dispatch bytecode instructions = implement instruction opcode
		switch (instruction = READ_BYTE()) {
			case OP_RETURN: {
				//Exit interpreter
				return INTERPRET_OK;
			}

			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				push_stack(constant);
				break;
			}
			case OP_NIL: push_stack(NIL_VAL); break;
			case OP_TRUE: push_stack(BOOL_VAL(true)); break;
			case OP_FALSE: push_stack(BOOL_VAL(false)); break;
			case OP_POP: pop_stack(); break;
			case OP_GET_LOCAL: {
				//Get stack slot index from local var from instruction operand 
				uint8_t slot = READ_BYTE();
				//Instructions work with data on top of stack
				//Push value on top of stack (copy)
				push_stack(vm.stack[slot]);
				break;
			}
			case OP_SET_LOCAL: {
				//Get stack slot index from local var from instruction operand 
				uint8_t slot = READ_BYTE();
				//Set that var to last value pushed on stack
				//Don't pop of stack
				//Value of assignment expression = the assigned value
				vm.stack[slot] = peek(0);
				break;
			}
			case OP_GET_GLOBAL: {
				ObjString* name = READ_STRING();
				Value value;
				if(!table_get(&vm.globals, name, &value)) {
					runtime_error("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				push_stack(value);
				break;
			}
			case OP_DEFINE_GLOBAL: {
				ObjString* name = READ_STRING();
				table_set(&vm.globals, name, peek(0));
				pop_stack();
				break;
			}
			case OP_SET_GLOBAL: {
				ObjString* name = READ_STRING();
				//If table_set returns true, it means we defined a new entry
				//And did not reassign an existing var
				//So we delete it again and return an error
				if(table_set(&vm.globals, name, peek(0))) {
					table_delete(&vm.globals, name);
					runtime_error("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_EQUAL: {
				Value b = pop_stack();
				Value a = pop_stack();
				push_stack(BOOL_VAL(values_equal(a, b)));
				break;
			}
			case OP_GREATER:  BINARY_OP(BOOL_VAL, > ); break;
			case OP_LESS:     BINARY_OP(BOOL_VAL, < ); break;
			case OP_ADD: {
				if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
					concatenate();
				}
				else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
					double b = AS_NUMBER(pop_stack());
					double a = AS_NUMBER(pop_stack());
					push_stack(NUMBER_VAL(a + b));
				}
				else {
					runtime_error(
						"Operands must be two numbers or two strings.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
			case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
			case OP_DIVIDE:   BINARY_OP(NUMBER_VAL,/); break;
			case OP_NOT:
				push_stack(BOOL_VAL(is_falsey(pop_stack())));
				break;

			case OP_NEGATE: {
				if(!IS_NUMBER(peek(0))) {
					runtime_error("Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				//Get back top, unwrap value, negate it, wrap it and push negated version back
				push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
				break;
			}

			case OP_PRINT: {
				print_value(pop_stack());
				printf("\n");
				break;
			}
		}
	}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}


InterpretResult interpret(const char* source) {
	Chunk chunk;
	init_chunk(&chunk);

	if(!compile(source, &chunk)) {
		free_chunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

	InterpretResult result = run();

	free_chunk(&chunk);

	return result;
}