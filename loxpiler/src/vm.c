#include "vm.h"
#include <stdio.h>
#include "common.h"
#include "debug.h"

//Better to pass around VM with a pointer, but we use 1 global VM to make code a bit lighter
//We only need one anyways
VM vm;

static void reset_stack(void) {
	//Point at start of array
	vm.stackTop = vm.stack;
}

void init_vm(void) {
	reset_stack();
}

void free_vm(void) {
	
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

//Beating heart of the VM
static InterpretResult run(void) {
#define READ_BYTE()(*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
	//Do while is a trick to make sure every statement is in same scope
	//And can use a semicolon at end
#define BINARY_OP(op) \
    do { \
      double b = pop(); \
      double a = pop(); \
      push(a op b); \
    } while (false)

	for(;;) {

#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
			printf("[ ");
			print_value(*slot);
			printf(" ] TOP");
		}
		printf("\n");
		disassemble_instruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		uint8_t instruction;

		//Dispatch bytecode instructions = implement instruction opcode
		switch (instruction = READ_BYTE()) {
			case OP_RETURN: {
				print_value(pop_stack());
				printf("\n");
				return INTERPRET_OK;
			}

			case OP_CONSTANT: {
				Value constant = READ_CONSTANT();
				push_stack(constant);
				break;
			}

			case OP_ADD:      BINARY_OP(+); break;
			case OP_SUBTRACT: BINARY_OP(-); break;
			case OP_MULTIPLY: BINARY_OP(*); break;
			case OP_DIVIDE:   BINARY_OP(/); break;

			case OP_NEGATE: {
				//Get back top and push negated version back
				push_stack(-pop_stack());
				break;
			}
		}
	}

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}


InterpretResult interpret(Chunk* chunk) {
	vm.chunk = chunk;
	//point at first instruction of code
	vm.ip = vm.chunk->code;
	return run();
}