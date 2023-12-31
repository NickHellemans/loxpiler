﻿#pragma once

#include "common.h"
#include "value.h"

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : (capacity) *2)

#define GROW_ARRAY(type, pointer, oldCap, newCap) \
	(type*) reallocate(pointer, sizeof(type) * (oldCap), \
		sizeof(type) * (newCap))

#define FREE_ARRAY(type, pointer, oldCap) \
	reallocate(pointer, sizeof(type) * (oldCap), 0)

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

//Size args control which operation to perform
//oldSize 		newSize 					Operation
//---------------------------------------------------------
//0 			Non‑zero 				Allocate new block.
//Non‑zero 			0 					Free allocation.
//Non‑zero 	Smaller than oldSize 	Shrink existing allocation.
//Non‑zero 	Larger than oldSize 	Grow existing allocation.
void* reallocate(void* ptr, size_t oldCap, size_t newCap);
void mark_object(Obj* object);
void mark_value(Value value);
void collect_garbage(void);
void free_objects(void);