#include "memory.h"
#include <stdlib.h>

#include "object.h"
#include "value.h"
#include "vm.h"


void* reallocate(void* ptr, size_t oldCap, size_t newCap) {

	if(newCap == 0) {
		free(ptr);
		return NULL;
	}

	void* result = realloc(ptr, newCap);

	if (result == NULL) 
		exit(1);

	return result;
}

void free_object(Obj* obj) {
	switch (obj->type) {

		case OBJ_FUNCTION:
			ObjFunction* fn = (ObjFunction*)obj;
			free_chunk(&fn->chunk);
			FREE(ObjFunction, obj);
			//Let gc deal with name (string)
			break;

		case OBJ_CLOSURE:
			//Only free ObjClosure itself not the ObjFunction
			//Closure doesn't own the fn, could be multiple closures referencing same fn
			ObjClosure* closure = (ObjClosure*)obj;
			FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
			FREE(ObjClosure, obj);
			break;

		case OBJ_STRING:
			ObjString* string = (ObjString*)obj;
			FREE_ARRAY(char, string->chars, string->length + 1);
			FREE(ObjString, obj);
			break;

		case OBJ_NATIVE:
			FREE(ObjNative, obj);
			break;

		case OBJ_UPVALUE:
			FREE(ObjUpvalue, obj);
			break;
	}
}

void free_objects(void) {
	Obj* curr = vm.objects;

	while(curr != NULL) {
		Obj* trash = curr;
		curr = curr->next;
		free_object(trash);
	}
}