#pragma once
#include <stdbool.h>
#include "chunk.h"
#include "object.h"

ObjFunction* compile(const char* source);
void mark_compiler_roots(void);
