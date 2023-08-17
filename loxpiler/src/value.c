#include "value.h"

#include <stdio.h>

#include "memory.h"

void init_value_array(ValueArray* array) {
	array->size = 0;
	array->capacity = 0;
	array->values = NULL;
}

void write_value_array(ValueArray* array, Value value) {
	if (array->capacity < array->size + 1) {
		int oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(Value, array->values,
			oldCapacity, array->capacity);
	}

	array->values[array->size] = value;
	array->size++;
}

void free_value_array(ValueArray* array) {
	FREE_ARRAY(Value, array->values, array->capacity);
	init_value_array(array);
}

void print_value(Value value) {
	printf("%g", value);
}