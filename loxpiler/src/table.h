#pragma once

#include "common.h"
#include "value.h"

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	int size;
	int cap;
	Entry* elements;
} Table;

void init_table(Table* table);
void free_table(Table* table);
bool table_get(Table* table, ObjString* key, Value* value);
bool table_set(Table* table, ObjString* key, Value value);
bool table_delete(Table* table, ObjString* key);
void table_add_all(Table* from, Table* to);