#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "scanner.h"
#include "object.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

static void statement(void);
static void declaration(void);

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

typedef void (*ParseFn)(bool canAssign);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct {
	Token name;
	int depth;
} Local;

typedef struct {
	Local locals[UINT8_COUNT];
	int localCount;
	int scopeDepth;
} Compiler;

static ParseRule* get_rule(TokenType type);

//Global var to not have to pass around as ptr
Parser parser;
Compiler* current = NULL;

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

static bool check_type(TokenType type) {
	return parser.curr.type == type;
}

static bool match(TokenType type) {
	if(!check_type(type)) {
		return false;
	}
	advance();
	return true;
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

static void init_compiler(Compiler* compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	current = compiler;
}

static void end_compiler(void) {
	emit_return();

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassemble_chunk(current_chunk(), "code");
	}
#endif
}

static void begin_scope(void) {
	current->scopeDepth++;
}

static void end_scope(void) {
	current->scopeDepth--;
	//Pop local scoped vars off 
	while(current->localCount > 0 && current->locals[current->localCount - 1].depth >
		current->scopeDepth){
		emit_byte(OP_POP);
		current->localCount--;
	}
}

static void parse_precedence(Precedence precedence) {
	advance();
	ParseFn prefixRule = get_rule(parser.prev.type)->prefix;
	if(prefixRule == NULL) {
		error("Expect expression");
		return;
	}

	//Only consume assignment ( = token ) if it is in context of a low-precedence expression
	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while(precedence <= get_rule(parser.curr.type)->precedence){
		advance();
		ParseFn infixRule = get_rule(parser.prev.type)->infix;
		infixRule(canAssign);
	}

	//If = didn't get consumed and we could assign it means we have wrong
	//assignment target
	//So we consume it and return error
	if(canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target");
	}
}

static uint8_t identifier_constant(Token* name) {
	//Global vars are looked up by name at runtime
	//Whole string is too big to fit into bytecode
	//Store string in constant table and instruction refers to the name by index in constant table
	return make_constant(OBJ_VAL(copy_string(name->start, name->length)));
}

static bool identifiers_equal(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int resolve_local(Compiler* compiler, Token* name) {
	for(int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiers_equal(name, &local->name))
			return i;
	}

	return -1;
}

static void add_local(Token name) {
	if(current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;
}

static void declare_variable(void) {
	//Global scope
	if (current->scopeDepth == 0)
		return;

	Token* name = &parser.prev;

	for(int i = current->localCount - 1; i >= 0; i++) {
		Local* local = &current->locals[i];
		if(local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}
		if(identifiers_equal(name, &local->name)) {
			error("Already a variable with this name in this scope.");
		}
	}

	add_local(*name);
}

static uint8_t parse_variable(const char* errorMsg) {
	consume(TOKEN_IDENTIFIER, errorMsg);
	declare_variable();
	//Local vars are not looked up by name at runtime
	//No need to put them in constant table
	//Return dummy table index
	if (current->scopeDepth > 0)
		return 0;

	return identifier_constant(&parser.prev);
}

static void mark_initialized(void) {
	current->locals[current->localCount - 1].depth =
		current->scopeDepth;
}

static void define_variable(uint8_t global) {
	//Local vars are not looked up by name at runtime
	//No need to put them in constant table
	//Exit fn
	if (current->scopeDepth > 0) {
		mark_initialized();
		return;
	}
	emit_bytes(OP_DEFINE_GLOBAL, global);
}

static void expression(void) {
	parse_precedence(PREC_ASSIGNMENT);
}

static void block(void) {
	while (!check_type(TOKEN_RIGHT_BRACE) && !check_type(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void var_declaration(void) {
	uint8_t global = parse_variable("Expect variable name.");

	if(match(TOKEN_EQUAL)) {
		expression();
	}else {
		//Init to nil if no expression
		//var myVar;
		emit_byte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
	define_variable(global);
}

static void expression_statement(void) {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	//Make sure to remove it from stack
	//Statements have a net-0 effect on state of stack
	//= Evaluate expression and discard result
	emit_byte(OP_POP);
}

static void print_statement(void) {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value");
	emit_byte(OP_PRINT);
}

static void synchronize(void) {
	parser.panicMode = false;

	while(parser.curr.type != EOF) {
		//Find the end of a statement and exit panic mode
		if(parser.prev.type == TOKEN_SEMICOLON) {
			return;
		}
		//Find beginning of a statement end exit
		switch (parser.curr.type) {
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
			return;

		default:
			; // Do nothing.
		}
		//No end or beginning found -> check next token
		advance();
	}
}

static void declaration(void) {
	if(match(TOKEN_VAR)) {
		var_declaration();
	}else {
		statement();
	}

	if (parser.panicMode)
		synchronize();
}

static void statement(void) {
	if(match(TOKEN_PRINT)) {
		print_statement();
	} else if (match(TOKEN_LEFT_BRACE)) {
		begin_scope();
		block();
		end_scope();
	} else {
		expression_statement();
	}
}

static void number(bool canAssign) {
	double value = strtod(parser.prev.start, NULL);
	emit_constant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
	emit_constant(OBJ_VAL(copy_string(parser.prev.start + 1,
		parser.prev.length - 2)));
}

static void named_variable(Token name, bool canAssign) {

	uint8_t getOp, setOp;
	int arg = resolve_local(current, &name);
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else {
		arg = identifier_constant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if(canAssign && match(TOKEN_EQUAL)) {
		expression();
		emit_bytes(setOp, (uint8_t)arg);
	} else {
		emit_bytes(getOp, (uint8_t)arg);
	}
}

static void variable(bool canAssign) {
	named_variable(parser.prev, canAssign);
}

static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

static void unary(bool canAssign) {
	TokenType operatorType = parser.prev.type;

	//Compile the operand
	parse_precedence(PREC_UNARY);

	//Emit negate bytecode after operand has been parsed and emitted into bytecode
	switch (operatorType) {
		case TOKEN_BANG: emit_byte(OP_NOT); break;
		case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
		default: return;
	}

}

static void binary(bool canAssign) {
	//Left hand operand has been compiled + infix operator consumed
	//Value will end up on stack
	//Binary will compile right hand of operand and emits the right bytecode instruction for the operator
	//When run, the VM will execute the left and right operand code, in that order, leaving their values on the stack.
	//Then it executes the instruction for the operator. That pops the two values, computes the operation, and pushes the result.

	TokenType operatorType = parser.prev.type;
	ParseRule* rule = get_rule(operatorType);

	parse_precedence((Precedence)(rule->precedence + 1));

	switch (operatorType) {
		case TOKEN_BANG_EQUAL:    emit_bytes(OP_EQUAL, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL:   emit_byte(OP_EQUAL); break;
		case TOKEN_GREATER:       emit_byte(OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emit_bytes(OP_LESS, OP_NOT); break;
		case TOKEN_LESS:          emit_byte(OP_LESS); break;
		case TOKEN_LESS_EQUAL:    emit_bytes(OP_GREATER, OP_NOT); break;
		case TOKEN_PLUS:          emit_byte(OP_ADD); break;
		case TOKEN_MINUS:         emit_byte(OP_SUBTRACT); break;
		case TOKEN_STAR:          emit_byte(OP_MULTIPLY); break;
		case TOKEN_SLASH:         emit_byte(OP_DIVIDE); break;
		default: return;
	}
}

static void literal(bool canAssign) {
	switch (parser.prev.type) {
		case TOKEN_FALSE: emit_byte(OP_FALSE); break;
		case TOKEN_NIL: emit_byte(OP_NIL); break;
		case TOKEN_TRUE: emit_byte(OP_TRUE); break;
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
  [TOKEN_BANG] =			{unary,     NULL,  PREC_NONE},
  [TOKEN_BANG_EQUAL] =		{NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL] =		{NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER] =			{NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] =	{NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS] =			{NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL] =		{NULL,     binary, PREC_COMPARISON},
 [TOKEN_IDENTIFIER] =		{variable, NULL,   PREC_NONE},
  [TOKEN_STRING] =			{string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER] =			{number,   NULL,   PREC_NONE},
  [TOKEN_AND] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE] =			{literal,  NULL,   PREC_NONE},
  [TOKEN_FOR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_IF] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL] =				{literal,     NULL,   PREC_NONE},
  [TOKEN_OR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE] =			{literal,     NULL,   PREC_NONE},
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
	Compiler compiler; init_compiler(&compiler);
	compilingChunk = chunk;
	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while(!match(TOKEN_EOF)) {
		declaration();
	}

	end_compiler();
	return !parser.hadError;
}
