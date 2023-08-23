#pragma once

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_FUNCTION(value)     is_obj_type(value, OBJ_FUNCTION)
#define IS_STRING(value)       is_obj_type(value, OBJ_STRING)

#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
	OBJ_STRING,
	OBJ_FUNCTION,
} ObjType;

struct Obj {
	ObjType type;
	//For GC
	Obj* next;
};

typedef struct {
	Obj obj;
	//number of parameters
	int arity;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

struct ObjString {
	Obj obj;
	int length;
	char* chars;
	//Calc hash upfront , strings are immutable, and store
	uint32_t hash;
};

void print_object(Value value);
ObjFunction* new_function(void);
ObjString* take_string(char* chars, int length);
ObjString* copy_string(const char* chars, int length);
static inline bool is_obj_type(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}