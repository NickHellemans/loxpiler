#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "common.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
	Token curr;
	Token prev;
	bool hadError;
	bool panicMode;
} Parser;

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,  // =
	PREC_OR,          // or
	PREC_AND,         // and
	PREC_EQUALITY,    // == !=
	PREC_COMPARISON,  // < > <= >=
	PREC_TERM,        // + -
	PREC_FACTOR,      // * /
	PREC_UNARY,       // ! -
	PREC_CALL,        // . ()
	PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(void);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

static ParseRule* get_rule(TokenType type);

//Global var to not have to pass around as ptr
Parser parser;

Chunk* compilingChunk;

static Chunk* current_chunk(void) {
	return compilingChunk;
}

static void error_at(Token* token, const char* message) {

	//Avoid cascading errors
	if (parser.panicMode)
		return;

	parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR) {
		// Nothing.
	}
	else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

static void error(const char* message) {
	error_at(&parser.prev, message);
}

static void error_at_current(const char* message) {
	error_at(&parser.curr, message);
}

static void advance(void) {
	parser.prev = parser.curr;

	//Keep looping until we reach a non-error or end
	for(;;) {
		parser.curr = scan_token();
		if (parser.curr.type != TOKEN_ERROR)
			break;

		error_at_current(parser.curr.start);
	}
}

static void consume(TokenType type, const char* message) {
	if(parser.curr.type == type) {
		advance();
		return;
	}

	error_at_current(message);
}

static void emit_byte(uint8_t byte) {
	write_chunk(current_chunk(), byte, parser.prev.line);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2) {
	emit_byte(byte1);
	emit_byte(byte2);
}

static void emit_return(void) {
	emit_byte(OP_RETURN);
}

static uint8_t make_constant(Value value) {
	int constant = add_constant(current_chunk(), value);
	if(constant > UINT8_MAX) {
		error("Too many constants in 1 chunk.");
		return 0;
	}

	return (uint8_t) constant;
}

static void emit_constant(Value value) {
	emit_bytes(OP_CONSTANT, make_constant(value));
}

static void end_compiler(void) {
	emit_return();

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassemble_chunk(current_chunk(), "code");
	}
#endif
}

static void parse_precedence(Precedence precedence) {
	advance();
	ParseFn prefixRule = get_rule(parser.prev.type)->prefix;
	if(prefixRule == NULL) {
		error("Expect expression");
		return;
	}

	prefixRule();

	while(precedence <= get_rule(parser.curr.type)->precedence){
		advance();
		ParseFn infixRule = get_rule(parser.prev.type)->infix;
		infixRule();
	}
}


static void expression(void) {
	parse_precedence(PREC_ASSIGNMENT);
}

static void number() {
	double value = strtod(parser.prev.start, NULL);
	emit_constant(value);
}


static void grouping(void) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

static void unary(void) {
	TokenType operatorType = parser.prev.type;

	//Compile the operand
	parse_precedence(PREC_UNARY);

	//Emit negate bytecode after operand has been parsed and emitted into bytecode
	switch (operatorType) {
		case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
		default: return;
	}

}

static void binary(void) {
	//Left hand operand has been compiled + infix operator consumed
	//Value will end up on stack
	//Binary will compile right hand of operand and emits the right bytecode instruction for the operator
	//When run, the VM will execute the left and right operand code, in that order, leaving their values on the stack.
	//Then it executes the instruction for the operator. That pops the two values, computes the operation, and pushes the result.

	TokenType operatorType = parser.prev.type;
	ParseRule* rule = get_rule(operatorType);

	parse_precedence((Precedence)(rule->precedence + 1));

	switch (operatorType) {
		case TOKEN_PLUS:          emit_byte(OP_ADD); break;
		case TOKEN_MINUS:         emit_byte(OP_SUBTRACT); break;
		case TOKEN_STAR:          emit_byte(OP_MULTIPLY); break;
		case TOKEN_SLASH:         emit_byte(OP_DIVIDE); break;
		default: return;
	}
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN] =		{grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS] =			{unary,    binary, PREC_TERM},
  [TOKEN_PLUS] =			{NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH] =			{NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR] =			{NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER_EQUAL] =	{NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS_EQUAL] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_IDENTIFIER] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_NUMBER] =			{number,   NULL,   PREC_NONE},
  [TOKEN_AND] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_FOR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_IF] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_OR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_VAR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF] =				{NULL,     NULL,   PREC_NONE},
};

static ParseRule* get_rule(TokenType type) {
	return &rules[type];
}

bool compile(const char* source, Chunk* chunk) {
	init_scanner(source);
	compilingChunk = chunk;
	parser.hadError = false;
	parser.panicMode = false;

	advance();
	expression();
	consume(TOKEN_EOF, "Expect end of expression.");
	end_compiler();
	return !parser.hadError;
}
