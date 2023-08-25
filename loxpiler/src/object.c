#include "object.h"

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "vm.h"

static void print_function(ObjFunction* function);

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocate_object(sizeof(type), objectType)

static Obj* allocate_object(size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;
	object->isMarked = false;
	//Insert at head of GC linked list
	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
	return object;
}

ObjClass* new_class(ObjString* name) {
	//Klass so it is easy to compile for c++ where class is keyword
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	init_table(&klass->methods);
	return klass;
}

ObjInstance* new_instance(ObjClass* klass) {
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	init_table(&instance->fields);
	return instance;
}

ObjBoundMethod* new_bound_method(Value receiver, ObjClosure* method) {
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

ObjFunction* new_function(void) {
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	init_chunk(&function->chunk);
	return function;
}

ObjClosure* new_closure(ObjFunction* function) {

	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjNative* new_native(NativeFn function) {
	ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

static ObjString* allocate_string(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;

	push_stack(OBJ_VAL(string));
	//Store string in interned strings table (more like a hashset)
	//Only care about keys, so val = nil
	table_set(&vm.strings, string, NIL_VAL);
	pop_stack();
	return string;
}

//FNV-1a hash
static uint32_t hash_string(const char* key, int length) {
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}

void print_object(Value value) {
	switch (OBJ_TYPE(value)) {

		case OBJ_CLASS:
			printf("%s", AS_CLASS(value)->name->chars);
			break;

		case OBJ_INSTANCE:
			printf("%s instance",
				AS_INSTANCE(value)->klass->name->chars);
			break;

		case OBJ_BOUND_METHOD:
			print_function(AS_BOUND_METHOD(value)->method->function);
			break;

		case OBJ_FUNCTION:
			print_function(AS_FUNCTION(value));
			break;

		case OBJ_CLOSURE:
			print_function(AS_CLOSURE(value)->function);
			break;

		case OBJ_STRING:
			printf("%s", AS_CSTRING(value));
			break;

		case OBJ_NATIVE:
			printf("<native fn>");
			break;

		case OBJ_UPVALUE:
			printf("upvalue");
			break;
	}
}

ObjString* take_string(char* chars, int length) {
	uint32_t hash = hash_string(chars, length);
	//Look if string already exists in interned strings table
	//if it does, just return that string instead of allocating a new one
	ObjString* interned = table_find_string(&vm.strings, chars, length, hash);
	if (interned != NULL) {
		//Need to free memory of passed in string
		//Ownership is being passed to this func
		FREE_ARRAY(char, chars, length + 1);
		return interned;
	}
	return allocate_string(chars, length, hash);
}

ObjString* copy_string(const char* chars, int length) {
	uint32_t hash = hash_string(chars, length);
	//Look if string already exists in interned strings table
	//if it does, just return that string instead of allocating a new one
	ObjString* interned = table_find_string(&vm.strings, chars, length, hash);
	if (interned != NULL)
		return interned;

	char* heapChars = ALLOCATE(char, length + 1);
	memcpy(heapChars, chars, length);
	heapChars[length] = '\0';
	return allocate_string(heapChars, length, hash);
}

ObjUpvalue* new_upvalue(Value* slot) {
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->closed = NIL_VAL;
	upvalue->location = slot;
	upvalue->next = NULL;
	return upvalue;
}

static void print_function(ObjFunction* function) {
	if (function->name == NULL) {
		printf("<script>");
		return;
	}
	printf("<fn %s>", function->name->chars);
}