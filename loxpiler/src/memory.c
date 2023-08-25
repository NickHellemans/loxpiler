#include "memory.h"
#include <stdlib.h>

#include "compiler.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* ptr, size_t oldCap, size_t newCap) {

	vm.bytesAllocated += newCap - oldCap;

	if(newCap > oldCap) {

#ifdef DEBUG_STRESS_GC
		//Force GC on every memory allocation
		collect_garbage();
#endif
		//Collect garbage after threshold of max bytes allocated is reached and we are allocating memory
		if (vm.bytesAllocated > vm.nextGC) {
			collect_garbage();
		}
	}


	if(newCap == 0) {
		free(ptr);
		return NULL;
	}

	void* result = realloc(ptr, newCap);

	if (result == NULL) 
		exit(1);

	return result;
}

void mark_object(Obj* object) {
	if (object == NULL) return;
	//Break out cycles if already marked
	if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)object);
	print_value(OBJ_VAL(object));
	printf("\n");
#endif

	object->isMarked = true;

	if (vm.grayCapacity < vm.grayCount + 1) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		//Call to system realloc: Memory in graystack is not managed by GC
		vm.grayStack = (Obj**)realloc(vm.grayStack,sizeof(Obj*) * vm.grayCapacity);
		if (vm.grayStack == NULL) 
			exit(1);
	}

	vm.grayStack[vm.grayCount++] = object;
}

void mark_value(Value value) {

	//Only mark heap allocated objects (no need to free stack allocated objects)
	if (IS_OBJ(value))
		mark_object(AS_OBJ(value));
}

static void mark_array(ValueArray* array) {
	for (int i = 0; i < array->size; i++) {
		mark_value(array->values[i]);
	}
}

static void blacken_object(Obj* object) {
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)object);
	print_value(OBJ_VAL(object));
	printf("\n");
#endif

	//Mark references based on obj type
	switch (object->type) {

		case OBJ_CLASS:
			ObjClass* klass = (ObjClass*)object;
			mark_object((Obj*)klass->name);
			break;

		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)object;
			mark_object((Obj*)instance->klass);
			mark_table(&instance->fields);
			break;
		}

		case OBJ_CLOSURE: {
			ObjClosure* closure = (ObjClosure*)object;
			mark_object((Obj*)closure->function);
			for (int i = 0; i < closure->upvalueCount; i++) {
				mark_object((Obj*)closure->upvalues[i]);
			}
			break;
		}

		case OBJ_FUNCTION: {
			ObjFunction* function = (ObjFunction*)object;
			mark_object((Obj*)function->name);
			mark_array(&function->chunk.constants);
			break;
		}

		case OBJ_UPVALUE:
			mark_value(((ObjUpvalue*)object)->closed);
			break;

		//No references
		case OBJ_NATIVE:
		case OBJ_STRING:
			break;
	}
}

void free_object(Obj* obj) {

#ifdef DEBUG_LOG_GC
	printf("%p free type %d\n", (void*)obj, obj->type);
#endif

	switch (obj->type) {

		case OBJ_CLASS: {
			FREE(ObjClass, obj);
			break;
		}

		case OBJ_INSTANCE: {
			ObjInstance* instance = (ObjInstance*)obj;
			//Only free table and not the entries in table
			//Might be other references to those objects
			//GC will take care of those
			free_table(&instance->fields);
			FREE(ObjInstance, obj);
			break;
		}

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

void mark_roots(void) {
	//Walk the VM's stack for locals and temporaries
	for(Value* slot = vm.stack; slot < vm.stackTop; slot++) {
		mark_value(*slot);
	}

	//Mark every closure in stack of call frames
	for(int i = 0; i < vm.frameCount; i++) {
		mark_object((Obj*)vm.frames[i].closure);
	}

	//Mark open upvalues list 
	for(ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
		mark_object((Obj*) upvalue);
	}

	//Mark globals
	mark_table(&vm.globals);

	//GC can run in compiling phase, values compiler accesses need to be marked
	mark_compiler_roots();
}

void trace_references(void) {
	//While worklist is not empty -> keep pulling out gray objects and traverse references
	//These references become new gray values in the worklist to mark their references too
	while(vm.grayCount > 0) {
		Obj* obj = vm.grayStack[--vm.grayCount];
		blacken_object(obj);
	}
}

void sweep(void) {
	Obj* prev = NULL;
	Obj* object = vm.objects;

	//Walk the list of all allocated objects
	while(object != NULL) {
		if(object->isMarked) {
			//Mark false for next GC
			object->isMarked = false;
			prev = object;
			object = object->next;
		} else {
			Obj* trash = object;
			object = object->next;

			//Removing head or not
			if(prev != NULL) {
				prev->next = object;
			} else {
				vm.objects = object;
			}
			free_object(trash);
		}
	}
}

void collect_garbage(void) {
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm.bytesAllocated;
#endif

	mark_roots();
	trace_references();
	table_remove_white(&vm.strings);
	sweep();
	//Adjust threshold based on live memory * a grow factor
	vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
	printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated,
		vm.nextGC);
#endif
}

void free_objects(void) {
	Obj* curr = vm.objects;

	while(curr != NULL) {
		Obj* trash = curr;
		curr = curr->next;
		free_object(trash);
	}

	free(vm.grayStack);
}