#include "compiler.h"

#include <stdio.h>

#include "common.h"
#include "scanner.h"

void compile(const char* source) {
	init_scanner(source);

	int line = -1;

	for(;;) {

		Token token = scan_token();
		if(token.line != line) {
			printf("%4d ", token.line);
			line = token.line;
		}
		else {
			printf("   | ");
		}

		//%.*s lets u pass precision as argument (number of chars to show)
		//Token points into source string and does not have a terminator
		printf("%2d '%.*s'\n", token.type, token.length, token.start);

		if (token.type == TOKEN_EOF) 
			break;
	}
}
