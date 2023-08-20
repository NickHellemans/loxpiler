#include "object.h"

#include <stdio.h>
#include <string.h>
#include "memory.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocate_object(sizeof(type), objectType)

static Obj* allocate_object(size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;

	//Insert at head of GC linked list
	object->next = vm.objects;
	vm.objects = object;
	return object;
}

static ObjString* allocate_string(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->length = length;
	string->chars = chars;
	string->hash = hash;
	//Store string in interned strings table (more like a hashset)
	//Only care about keys, so val = nil
	table_set(&vm.strings, string, NIL_VAL);
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
		case OBJ_STRING:
			printf("%s", AS_CSTRING(value));
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
