#pragma once
#include <stdbool.h>
#include "chunk.h"
#include "object.h"

ObjFunction* compile(const char* source);
