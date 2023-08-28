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
	//Modulo is very slow
	//We can take advantage of the fact that we know more about our problem than the CPU does
	//We use modulo here to take the hash and wrap it to fit inside the table bounds
	//The array size always start at 8 and grows by a factor of 2
	//We can use bit masking  to calculate the remainder of a power of 2
	//In binary the remainder is equal to the the dividend with the highest 2 bits shaved off
	//Those bits are the bits at or left of the divisor's single 1 bit
	//Example:
	//229 % 64 = 37
	//11100101 % 01000000 = 00100101
	//Instead we can use AND with the divisor - 1
	//Subtracting 1 from a power of 2, gives u a series of 1 bits
	//That is exactly the mask we need in order to strip out those 2 leftmost bits
	//229 & 63 = 37
	//11100101 & 00111111 = 00100101
	//uint32_t index = key->hash % cap;
	uint32_t index = key->hash & (cap - 1);
	//Check for tombstones while linear probing to not miss any possible entries

	Entry* tombstone = NULL;
	for(;;) {
		Entry* entry = &entries[index];
		if(entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				
				//Empty entry
				//If tombstone is not NULL it means we passed a tombstone
				//so we return that location instead of current entry to reuse it to avoid wasting space
				//else return entry
				return tombstone != NULL ? tombstone : entry;
			} else {
				//Found tombstone, update var and keep going with next location
				if (tombstone == NULL) tombstone = entry;
			}
		}
		//Found key
		//We can compare pointers because string interning
		//String compare is slow (loop over every char)
		else if (entry->key == key)
			return entry;
		
		//Linear probing to handle collisions
		//index = (index + 1) % cap;
		index = (index + 1) & (cap - 1);
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
		
		if(entry->key == NULL)
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

	if(table->size + 1 > table->cap * TABLE_MAX_LOAD) {
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
	if (table->size == 0) return NULL;

	//uint32_t index = hash % table->cap;
	uint32_t index = hash & (table->cap - 1);
	for(;;) {
		Entry* entry = &table->elements[index];
		if(entry->key == NULL) {
			//Stop if non empty, non tombstone entry
			if (IS_NIL(entry->value)) return NULL;

		} else if(entry->key->length == length && entry->key->hash == hash && memcmp(entry->key->chars, chars, length) == 0) {
			//Found it
			return entry->key;
		}
		//index = (index + 1 ) % table->cap;
		index = (index + 1) & (table->cap - 1);
	}
}

void table_remove_white(Table* table) {
	//Clear out dangling pointers for to be freed memory
	//Remove references to strings that will be swept after this
	for (int i = 0; i < table->cap; i++) {
		Entry* entry = &table->elements[i];
		if (entry->key != NULL && !entry->key->obj.isMarked) {
			table_delete(table, entry->key);
		}
	}
}

void mark_table(Table* table) {
	for(int i = 0; i < table->cap; i++) {
		Entry* entry = &table->elements[i];
		mark_object((Obj*)entry->key);
		mark_value(entry->value);
	}
}