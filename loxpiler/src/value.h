#pragma once

#include <string.h>

#include "common.h"

//Struct inheritance 
typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
//Set of quiet NaN bits
//If these bits are set (NaN bits + quiet NaN bit + one more), we can be certain it is one of the bit patterns we set aside for other types
#define QNAN     ((uint64_t)0x7ffc000000000000)

//Only need 1 bit pattern to represent each singleton value (nil, false + true)
//Claim 2 lowest bits of our unused mantissa space as a "type tag" for these singleton values
//So the representation of nil = QNAN bits + nil type tag
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

typedef uint64_t Value;

//If it ain't true, it is false
#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_NUMBER(value)    value_to_num(value)
//Get pointer bits out by clearing sign bit + QNAN bits
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) num_to_value(num)
//Use sign bit (MSB) as type tag for objects
//If sign bit is set, remaining low bits store the pointer to the object
//Convert Obj pointer to Value
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

//OR 1:
//It becomes TRUE_VAL if it is a FALSE_VAL
//Stays TRUE_VAL if it it a TRUE_VAL
//Something else, non boolean
#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL)
//Check equality on uint64_t because nil only has 1 bit representation = same uint64_t number
#define IS_NIL(value)       ((value) == NIL_VAL)
//Mask out all of the bits except for QNAN set of bits, if all of those are set it is a NaN boxed value of another type otherwise it is a number
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define IS_OBJ(value) \
    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

static inline double value_to_num(Value value) {
	double num;
	memcpy(&num, &value, sizeof(Value));
	return num;
}

static inline Value num_to_value(double num) {
	//Copy bits over from a double and treat as a Value type
	Value value;
	memcpy(&value, &num, sizeof(double));
	return value;
}

#else

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

#endif

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