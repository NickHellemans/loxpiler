#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "value.h"

//Better to pass around VM with a pointer, but we use 1 global VM to make code a bit lighter
//We only need one anyways
VM vm;

static Value clock_native(int argCount, Value* args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void reset_stack(void) {
	//Point at start of array
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

static void runtime_error(const char* format, ...) {
	va_list args;
	va_start(args, format);

	vfprintf(stderr, format, args);

	va_end(args);
	fputs("\n", stderr);

	//Print stack trace
	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;
		fprintf(stderr, "[line %d] in ",
			function->chunk.lines[instruction]);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	reset_stack();
}

static void define_native(const char* name, NativeFn function) {
	push_stack(OBJ_VAL(copy_string(name, (int)strlen(name))));
	push_stack(OBJ_VAL(new_native(function)));
	table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop_stack();
	pop_stack();
}

void init_vm(void) {
	reset_stack();
	vm.objects = NULL;
	vm.bytesAllocated = 0;
	vm.nextGC = (size_t) 1024 * 1024;
	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	init_table(&vm.globals);
	init_table(&vm.strings);

	//Initialize to null so if copy_string triggers a GC it does not read into uninitialized memory
	vm.initString = NULL;
	vm.initString = copy_string("init", 4);

	define_native("clock", clock_native);
}

void free_vm(void) {
	free_table(&vm.globals);
	free_table(&vm.strings);
	//Clear pointer since next line will free it
	vm.initString = NULL;
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

static bool call (ObjClosure* closure, int argCount) {

	//Too many args passed in
	if (argCount != closure->function->arity) {
		runtime_error("Expected %d arguments but got %d.", closure->function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtime_error("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm.stackTop - argCount - 1;
	return true;
}

static bool call_value(Value callee, int argCount) {
	if(IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee)) {

			case OBJ_BOUND_METHOD: {
				ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
				//Put receiver in the expected first slot
				//So when 'this' gets accessed in method the method will look for it in this spot
				vm.stackTop[-argCount - 1] = bound->receiver;
				return call(bound->method, argCount);
			}

			case OBJ_CLASS: {
				//Init class instance by using init method if there is one else return uninitialized instance
				ObjClass* klass = AS_CLASS(callee);
				vm.stackTop[-argCount - 1] = OBJ_VAL(new_instance(klass));

				Value initializer;
				if (table_get(&klass->methods, vm.initString, &initializer)) {
					return call(AS_CLOSURE(initializer), argCount);
				} else if (argCount != 0) {
					//If there is no init method it makes no sense to pass arguments when creating an instance
					runtime_error("Expected 0 arguments but got %d.",argCount);
					return false;
				}

				return true;
			}

			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);

			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(callee);
				Value result = native(argCount, vm.stackTop - argCount);
				vm.stackTop -= argCount + 1;
				push_stack(result);
				return true;
			}
			default:
				//Non callable
				break;
		}
	}
	runtime_error("Can only call functions and classes.");
	return false;
}

static bool invoke_from_class(ObjClass* klass, ObjString* name, int argCount) {
	//Get method from class by name and call it
	Value method;
	if (!table_get(&klass->methods, name, &method)) {
		runtime_error("Undefined property '%s'.", name->chars);
		return false;
	}
	//Push call onto call frame
	//No need to create a BoundMethod or juggle stack
	//Everything is already where it should be
	return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
	//Get instance method is called on
	Value receiver = peek(argCount);

	//Check if obj is an instance
	if (!IS_INSTANCE(receiver)) {
		runtime_error("Only instances have methods.");
		return false;
	}

	ObjInstance* instance = AS_INSTANCE(receiver);

	//Before looking up a method on the instance's class we look for a field with the same name, if we find a field we store it on the stack in place of the receiver, under the arg list so it ends up on right spot if it is a callable value

	//For example fields that return a callable value
	//If not callable 'call_value' will report an error
	Value value;
	if (table_get(&instance->fields, name, &value)) {
		vm.stackTop[-argCount - 1] = value;
		return call_value(value, argCount);
	}

	//Look for method by name
	return invoke_from_class(instance->klass, name, argCount);
}

static bool bind_method(ObjClass* klass, ObjString* name) {
	//Look for method with given name in given class
	Value method;
	if(!table_get(&klass->methods, name, &method)) {
		runtime_error("Undefined property '%s'.", name->chars);
		return false;
	}
	//Wrap method in BoundMethod together with receiver
	//The instance which is the receiver is on top of stack
	ObjBoundMethod* bound = new_bound_method(peek(0), AS_CLOSURE(method));

	//Replace values on stack: pop receiver and push bound method
	pop_stack();
	push_stack(OBJ_VAL(bound));
	return true;
}

static ObjUpvalue* capture_upvalue(Value* local) {

	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;
	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = new_upvalue(local);

	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void close_upvalues(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static void define_method(ObjString* name) {
	//Closure on top of stack
	Value method = peek(0);
	//Class sits after the closure
	ObjClass* klass = AS_CLASS(peek(1));

	//Set method in the table of specified class
	table_set(&klass->methods, name, method);
	//Pop closure
	pop_stack();
}

static bool is_falsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(void) {
	ObjString* b = AS_STRING(peek(0));
	ObjString* a = AS_STRING(peek(1));

	int length = b->length + a->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';
	ObjString* result = take_string(chars, length);
	pop_stack();
	pop_stack();
	push_stack(OBJ_VAL(result));
}

//Beating heart of the VM
static InterpretResult run(void) {
	//Store current topmost frame
	CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
	//Yank next 2 bytes out of code and build a 16bit integer
#define READ_SHORT() \
    (frame->ip += 2, \
    (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

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
		disassemble_instruction(&frame->closure->function->chunk,
			(int)(frame->ip - frame->closure->function->chunk.code));
#endif
		uint8_t instruction;

		//Dispatch bytecode instructions = implement instruction opcode
		switch (instruction = READ_BYTE()) {
			case OP_RETURN: {
				Value result = pop_stack();
				vm.frameCount--;
				if(vm.frameCount == 0) {
					pop_stack();
					return INTERPRET_OK;
				}

				vm.stackTop = frame->slots;
				push_stack(result);
				frame = &vm.frames[vm.frameCount - 1];
				break;
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
				push_stack(frame->slots[slot]);
				break;
			}
			case OP_SET_LOCAL: {
				//Get stack slot index from local var from instruction operand 
				uint8_t slot = READ_BYTE();
				//Set that var to last value pushed on stack
				//Don't pop of stack
				//Value of assignment expression = the assigned value
				frame->slots[slot] = peek(0);
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

			case OP_GET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				push_stack(*frame->closure->upvalues[slot]->location);
				break;
			}

			case OP_SET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				*frame->closure->upvalues[slot]->location = peek(0);
				break;
			}

			case OP_GET_PROPERTY: {

				if (!IS_INSTANCE(peek(0))) {
					runtime_error("Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjInstance* instance = AS_INSTANCE(peek(0));
				ObjString* name = READ_STRING();

				//Find field or method with given name
				//Replace top of stack with the accessed property

				//Try field first
				Value value;
				if (table_get(&instance->fields, name, &value)) {
					pop_stack();
					push_stack(value);
					break;
				}

				//Try method if field fails
				if(!bind_method(instance->klass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}

				break;
			}

			case OP_SET_PROPERTY: {
				if (!IS_INSTANCE(peek(1))) {
					runtime_error("Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjInstance* instance = AS_INSTANCE(peek(1));
				table_set(&instance->fields, READ_STRING(), peek(0));

				//Setter is an expression that results in the assigned value, so we need to leave that opn the stack
				Value value = pop_stack();
				pop_stack();
				push_stack(value);
				break;
			}

			case OP_GET_SUPER: {

				//Get method name for superclass
				ObjString* name = READ_STRING();
				//Get superclass and pop it from stack to leave instance at top of stack
				//When bind_method succeeds it pops off the instance and pushes the BoundMethod
				ObjClass* superclass = AS_CLASS(pop_stack());
				//Pass superclass and method name to create a BoundMethod to bundle closure and instance
				if(!bind_method(superclass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				//This heaps allocate a BoundMethod Obj every time, most of the time we want to invoke a supercall and the next instruction will be a OP_CALL that will unpack the BoundMethod and discard it
				//Compiler can tell if we immediately invoke it or not so we optimize supercalls to directly invoke it
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
			case OP_JUMP: {
				//Save offset in 16bit int (saved in 2 bytes)
				uint16_t offset = READ_SHORT();
				//Unconditional jump
				frame->ip += offset;
				break;
			}
			case OP_LOOP: {
				//Save offset in 16bit int (saved in 2 bytes)
				uint16_t offset = READ_SHORT();
				//Unconditional jump backwards
				frame->ip -= offset;
				break;
			}
			case OP_JUMP_IF_FALSE:
				//Save offset in 16bit int (saved in 2 bytes)
				uint16_t offset = READ_SHORT();
				//Check cond
				if(is_falsey(peek(0))) {
					frame->ip += offset;
				}
				break;

			case OP_CALL: {
				int argCount = READ_BYTE();
				if (!call_value(peek(argCount), argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}

			case OP_INVOKE: {
				//Get method name and arg count
				ObjString* method = READ_STRING();
				int argCount = READ_BYTE();
				
				if (!invoke(method, argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				//if success there is new call frame on stack so refresh cached frame
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}

			case OP_SUPER_INVOKE: {
				//Get method name and arg count
				ObjString* method = READ_STRING();
				int argCount = READ_BYTE();
				//Get superclass from stack and pop it off so stack is set up right for a method call
				ObjClass* superclass = AS_CLASS(pop_stack());
				//Look up given function by name and create a call for it
				//Pushes new frame on callstack if success 
				if (!invoke_from_class(superclass, method, argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				//Refresh frame
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}

			case OP_CLOSURE: {
				ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
				ObjClosure* closure = new_closure(function);
				push_stack(OBJ_VAL(closure));

				for (int i = 0; i < closure->upvalueCount; i++) {
					uint8_t isLocal = READ_BYTE();
					uint8_t index = READ_BYTE();
					if (isLocal) {
						closure->upvalues[i] = capture_upvalue(frame->slots + index);
					}
					else {
						closure->upvalues[i] = frame->closure->upvalues[index];
					}
				}
				break;
			}
			case OP_CLOSE_UPVALUE: {
				close_upvalues(vm.stackTop - 1);
				pop_stack();
				break;
			}

			case OP_CLASS: {
				push_stack(OBJ_VAL(new_class(READ_STRING())));
				break;
			}

			case OP_INHERIT:
				//Get superclass and check if it is a class
				Value superclass = peek(1);
				if(!IS_CLASS(superclass)) {
					runtime_error("Super class must be a class.");
					return INTERPRET_RUNTIME_ERROR;
				}
				ObjClass* subclass = AS_CLASS(peek(0));
				//Copy over all methods from super class to subclass
				//Table from subclass is empty so any method the subclass overrides will overwrite these entries
				table_add_all(&AS_CLASS(superclass)->methods, &subclass->methods);
				//Pop subclass
				pop_stack();
				break;

			case OP_METHOD:
				define_method(READ_STRING());
				break;
		}
	}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP

}


InterpretResult interpret(const char* source) {
	//Compiled top level code
	ObjFunction* function = compile(source);
	if (function == NULL) 
		return INTERPRET_COMPILE_ERROR;

	//Store on stack in reserved stack slot and prepare initial call frame to execute the code
	//Get interpreter ready to start executing code
	push_stack(OBJ_VAL(function));

	ObjClosure* closure = new_closure(function);
	pop_stack();
	push_stack(OBJ_VAL(closure));
	call(closure, 0);

	return run();
}