#include "table.h"
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "object.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void init_table(Table* table) {
	table->cap = 0;
	table->size = 0;
	table->elements = NULL;
}

void free_table(Table* table) {
	//Only need to free elements since we are using linear probing
	//No linked lists to free for each bucket
	FREE_ARRAY(Entry, table->elements, table->cap);
	init_table(table);
}

Entry* find_entry(Entry* entries, int cap, ObjString* key) {
	uint32_t index = key->hash % cap;

	for(;;) {
		Entry* entry = &entries[index];
		if(entry->key == key || entry->key == NULL) {
			return entry;
		}
		//Linear probing to handle collisions
		index = (index + 1) % cap;
	}
}

static void adjust_capacity(Table* table, int cap) {
	Entry* entries = ALLOCATE(Entry, cap);
	//Empty buckets
	for(int i = 0; i < cap; i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}

	//Rehash elems
	for(int i = 0; i < table->cap; i++) {
		Entry* entry = &table->elements[i];
		if(entry == NULL)
			continue;
		Entry* dest = find_entry(entries, cap, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
	}

	//Free old table
	FREE_ARRAY(Entry, table->elements, table->cap);

	table->elements = entries;
	table->cap = cap;
}

//Val output param
bool table_get(Table* table, ObjString* key, Value* value) {
	if (table->size == 0)
		return false;
	Entry* entry = find_entry(table->elements, table->cap, key);
	if (entry->key == NULL)
		return false;
	*value = entry->value;
	return true;
}

bool table_set(Table* table, ObjString* key, Value value) {

	if(table->size >= table->cap * TABLE_MAX_LOAD) {
		int capacity = GROW_CAPACITY(table->cap);
		adjust_capacity(table, capacity);
	}

	Entry* entry = find_entry(table->elements, table->cap, key);
	bool isNewKey = entry->key == NULL;

	if (isNewKey)
		table->size++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

void table_add_all(Table* from, Table* to) {
	for(int i = 0; i < from->cap; i++) {
		Entry* entry = &from->elements[i];
		if (entry->key != NULL)
			table_set(to, entry->key, entry->value);
	}
}