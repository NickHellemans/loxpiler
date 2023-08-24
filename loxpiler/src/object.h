#pragma once

#include "chunk.h"
#include "common.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)
#define IS_FUNCTION(value)     is_obj_type(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)      is_obj_type(value, OBJ_CLOSURE)
#define IS_NATIVE(value)       is_obj_type(value, OBJ_NATIVE)
#define IS_STRING(value)       is_obj_type(value, OBJ_STRING)

#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
	OBJ_FUNCTION,
	OBJ_CLOSURE,
	OBJ_NATIVE,
	OBJ_STRING,
	OBJ_UPVALUE,
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
	int upvalueCount;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

//Manage closed over variables that are no longer on stack
typedef struct ObjUpvalue {
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;

//Builtin fn
typedef Value(*NativeFn)(int argCount, Value* args);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

struct ObjString {
	Obj obj;
	int length;
	char* chars;
	//Calc hash upfront , strings are immutable, and store
	uint32_t hash;
};


void print_object(Value value);
ObjFunction* new_function(void);
ObjClosure* new_closure(ObjFunction* function);
ObjNative* new_native(NativeFn function);
ObjString* take_string(char* chars, int length);
ObjString* copy_string(const char* chars, int length);
ObjUpvalue* new_upvalue(Value* slot);
static inline bool is_obj_type(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}