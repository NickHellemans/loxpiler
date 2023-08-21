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
	//Check for tombstones while linear probing to not miss any possible entries

	Entry* tombstone = NULL;
	for(;;) {
		Entry* entry = &entries[index];
		if(entry->key == NULL) {
			if (IS_NIL(entry->value))
				//Empty entry
				//If tombstone is not NULL it means we passed a tombstone
				//so we return that location instead of current entry to reuse it to avoid wasting space
				//else return entry
				return tombstone != NULL ? tombstone : entry;
			else
				//Found tombstone, update var and keep going with next location
				if (tombstone == NULL)
					tombstone = entry;
		}
		//Found key
		//We can compare pointers because string interning
		//String compare is slow (loop over every char)
		else if (entry->key == key)
			return entry;
		
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

	//When rehashing we don't copy tombstones so need to recalc size
	//Rebuild probe sequences anyways
	table->size = 0;
	//Rehash elems
	for(int i = 0; i < table->cap; i++) {
		Entry* entry = &table->elements[i];
		if(entry == NULL)
			continue;
		Entry* dest = find_entry(entries, cap, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->size++;
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

	//Only increment count when we insert in a totally empty entry and not a tombstone or new key
	//Bucket with tombstone has been accounted for
	if (isNewKey && IS_NIL(entry->value))
		table->size++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

//Does not decrease size --> deleted entries turn into tombstones
//tombstones get counted in size 
bool table_delete(Table* table, ObjString* key) {
	if (table->size == 0)
		return false;
	//Look for entry
	Entry* entry = find_entry(table->elements,table->cap, key);
	//Not in table
	if (entry->key == NULL)
		return false;
	//Delete key from table and set tombstone (val = true)
	entry->key = NULL;
	entry->value = BOOL_VAL(true);
	return true;
}

void table_add_all(Table* from, Table* to) {
	for(int i = 0; i < from->cap; i++) {
		Entry* entry = &from->elements[i];
		if (entry->key != NULL)
			table_set(to, entry->key, entry->value);
	}
}

ObjString* table_find_string(Table* table, const char* chars, int length, uint32_t hash) {
	if (table->size == 0)
		return NULL;
	uint32_t index = hash % table->cap;
	for(;;) {
		Entry* entry = &table->elements[index];
		if(entry->key == NULL) {
			if (IS_NIL(entry->value))
				//Stop if non empty, non tombstone entry
				return NULL;
		} else if(entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length)) {
			//Found it
			return entry->key;
		}
		index = (index + 1 ) % table->cap;
	}
}