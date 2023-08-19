#pragma once

#include "common.h"

//Struct inheritance 
typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJ, //Values on heap
} ValueType;

typedef struct {
	ValueType type;
	union {
		bool boolean;
		double number;
		//Pointer to heap mem
		Obj* obj;
	} as;
} Value;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)	  ((value).type == VAL_OBJ)

#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)	  ((value).as.obj)

#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = (value)}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = (value)}})
#define OBJ_VAL(value)    ((Value){VAL_OBJ, {.obj = (Obj*)(value)}})

typedef struct  {
	int size;
	int capacity;
	Value* values;
} ValueArray;


void init_value_array(ValueArray* array);
void write_value_array(ValueArray* array, Value value);
void free_value_array(ValueArray* array);
void print_value(Value value);
bool values_equal(Value a, Value b);