#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "scanner.h"
#include "object.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

static void statement(void);
static void declaration(void);
static void expression(void);
static void named_variable(Token name, bool canAssign);

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
	//Scope depth of the block where the local variable was declared
	int depth;
	bool isCaptured;
} Local;

typedef struct {
	uint8_t index;
	bool isLocal;
} Upvalue;

typedef enum {
	TYPE_FUNCTION,
	TYPE_SCRIPT
} FunctionType;

typedef struct Compiler{
	struct Compiler* enclosing;
	//Ref to current function being built
	ObjFunction* function;
	//Compiling top-level code vs body of a function
	FunctionType type;
	//Array of local vars that are currently in scope during each point of compilation
	//Instruction operand = single byte -> Max locals = uint8 max (256 indexes available)
	Local locals[UINT8_COUNT];
	//How many locals are in scope atm / how many array slots are in use
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	//Number of blocks surrounding the current bit of code we are compiling
	//0 - global / 1 - 1 block nested / ...
	int scopeDepth;
} Compiler;

static ParseRule* get_rule(TokenType type);

//Global var to not have to pass around as ptr
Parser parser;
Compiler* current = NULL;

Chunk* compilingChunk;

static Chunk* current_chunk(void) {
	return &current->function->chunk;
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

static void emit_loop(int loopStart) {
	emit_byte(OP_LOOP);

	//Offset to jump back
	// +2 to jump over OP_LOOP operands (16bits)
	int offset = current_chunk()->size - loopStart + 2;
	if (offset > UINT16_MAX) 
		error("Body of loop too large.");
	//Fill operands off OP_LOOP instruction with offset value
	//Int -> 16bits
	emit_byte((offset >> 8) & 0xff);
	emit_byte(offset & 0xff);
}

static int emit_jump(uint8_t instruction) {
	emit_byte(instruction);
	//Fill operand with placeholder to "backpatch" later when we know real offset
	//16 bit offset -> 65 535 bytes of code we can jump over max
	emit_byte(0xff);
	emit_byte(0xff);
	//return index (location) of instruction in code so we can patch later
	return current_chunk()->size - 2;
}

static void emit_return(void) {
	emit_byte(OP_NIL);
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

static void patch_jump(int offset) {
	//Get amount of bytes to jump if we need to skip then branch
	// = land right after then branch
	// -2 to adjust for the bytecode for the jump offset itself.
	int jump = current_chunk()->size - offset - 2;

	if (jump > UINT16_MAX) {
		error("Too much code to jump over.");
	}
	//Patch jump instruction operand with calculated jump offset
	//Fit int in 16bits
	current_chunk()->code[offset] = (jump >> 8) & 0xff;
	current_chunk()->code[offset + 1] = jump & 0xff;
}

static void init_compiler(Compiler* compiler, FunctionType type) {
	compiler->enclosing = (struct Compiler*) current;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->function = new_function();
	current = compiler;
	//Store function name
	if (type != TYPE_SCRIPT) {
		current->function->name = copy_string(parser.prev.start,
			parser.prev.length);
	}

	//Claim first stack slot of locals for internal use
	//Empty name so user cannot write an identifier that refers to it
	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->name.start = "";
	local->name.length = 0;
}

static ObjFunction* end_compiler(void) {
	emit_return();
	ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassemble_chunk(current_chunk(), function->name != NULL
			? function->name->chars : "<script>");
	}
#endif
	current = (Compiler*) current->enclosing;
	//Return compiled function with all code in it
	return function;
}

static void begin_scope(void) {
	current->scopeDepth++;
}

static void end_scope(void) {
	current->scopeDepth--;
	//Pop local scoped vars off by reducing array size until we entered a new scope or no locals left
	//Loop backwards
	while(current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth){

		if (current->locals[current->localCount - 1].isCaptured) {
			//If local var is captured in closure we hoist it to heap
			emit_byte(OP_CLOSE_UPVALUE);
		}
		else {
			//Local vars occupy spot on stack
			//When it goes out of scope it is no longer needed
			//Pop it off
			emit_byte(OP_POP);
		}

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
	//Try to find a local var with parsed identifier name
	//Walk list of locals currently in scope backwards (last declared vars) to ensure inner local vars shadow outer declared locals with the same name in surrounding scopes
	//Compare local with identifier
	//If match --> return stack slot index
	//Locals array in compiler has exact same layout as the VM's stack at runtime
	//Variable index in locals array == stack slot 
	for(int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiers_equal(name, &local->name)) {

			//Check local scope depth to see if local var is fully initialized and not pointing to itself
			if (local->depth == -1) {
				error("Can't read local variable in its own initializer.");
			}
			return i;
		}
	}
	//Not found -> assume global var
	return -1;
}

static int add_upvalue(Compiler* compiler, uint8_t index, bool isLocal) {
	int upvalueCount = compiler->function->upvalueCount;

	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

static int resolve_upvalue(Compiler* compiler, Token* name) {
	if (compiler->enclosing == NULL) return -1;

	int local = resolve_local((Compiler*)compiler->enclosing, name);
	if (local != -1) {
		compiler->enclosing->locals[local].isCaptured= true;
		return add_upvalue(compiler, (uint8_t)local, true);
	}

	int upvalue = resolve_upvalue((Compiler*)compiler->enclosing, name);
	if (upvalue != -1) {
		return add_upvalue(compiler, (uint8_t)upvalue, false);
	}
	return -1;
}

static void add_local(Token name) {
	//If max amount of locals -> error and return
	if(current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}

	//Initialize the next available Local in the locals array of vars
	//Store variable name (identifier) and depth
	Local* local = &current->locals[current->localCount++];
	local->name = name;
	//Edge case when var declaration points back to itself
	//Indicate uninitialized state (Name declared but no value initialized)
	//Then we compile initializer, mark var as ready to use (initialized) if it does not point back to itself with an identifier in the expression (ex. var a = a;)
	local->depth = -1;
	local->isCaptured = false;
}

static void declare_variable(void) {
	//Global scope -> exit
	if (current->scopeDepth == 0)
		return;

	//Consumed identifier
	Token* name = &parser.prev;

	//Check for same named declared vars in SAME EXACT scope
	//Loop over the locals array backwards (current scope is always at the back of array)
	//Look for var with same name and report any errors if one in same scope
	for(int i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];
		//Break out of loop if local->depth is different than scopeDepth -> entered different scope
		//And local var is fully initialized
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
	//Exit if global
	if (current->scopeDepth == 0) 
		return;
	//Mark var as initialized by setting it's depth value to the current scopeDepth
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

static uint8_t argument_list(void) {
	//Parse argument expressions
	//Leaves values on stack in preparation for stack
	uint8_t argCount = 0;
	if(!check_type(TOKEN_RIGHT_PAREN)) {
		do {
			expression();
			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}
			argCount++;
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void and_(bool canAssign) {
	//Left hand expression will be compiled, value will be on top of stack at runtime
	//If that already is false -> entire expression will be false
	//So skip right hand and leave value on stack so result of entire expression is false (short circuit)
	int endJump = emit_jump(OP_JUMP_IF_FALSE);
	//If not false, discard value for left hand expression + parse right hand which becomes result of entire and expression
	emit_byte(OP_POP);
	parse_precedence(PREC_AND);
	patch_jump(endJump);
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

static void function(FunctionType type) {
	Compiler compiler;
	init_compiler(&compiler, type);
	//end_compiler ends scope
	begin_scope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
	if(!check_type(TOKEN_RIGHT_PAREN)) {
		do {
			current->function->arity++;
			if (current->function->arity > 255) {
				error_at_current("Can't have more than 255 parameters.");
			}
			uint8_t constant = parse_variable("Expect parameter name");
			define_variable(constant);
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block();

	ObjFunction* function = end_compiler();
	emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(function)));

	//Operand pairs per upvalue
	for (int i = 0; i < function->upvalueCount; i++) {
		emit_byte(compiler.upvalues[i].isLocal ? 1 : 0);
		emit_byte(compiler.upvalues[i].index);
	}
}

static void method(void) {
	//Method name
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint8_t constant = identifier_constant(&parser.prev);

	//Method parameters and body
	FunctionType type = TYPE_FUNCTION;
	function(type);
	emit_bytes(OP_METHOD, constant);
}

static void class_declaration(void) {
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.prev;
	//Add class name to surrounding function constant table as a string
	uint8_t nameConstant = identifier_constant(&parser.prev);
	//Bind class object to name
	declare_variable();
	//Create class object at runtime
	//Constant table index of the class's name as an operand
	emit_bytes(OP_CLASS, nameConstant);
	//Define variable before body
	//Refer to the containing class inside the bodies of its own methods
	define_variable(nameConstant);

	//Before binding methods load class back on top of stack so class is sitting under method's closure
	named_variable(className, false);
	//Compile body
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while(!check_type(TOKEN_RIGHT_BRACE) && !check_type(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	//Pop class off
	emit_byte(OP_POP);
}

static void fun_declaration(void) {
	//Get function name
	uint8_t global = parse_variable("Expect function name.");
	//Can't call function and execute the body until after it is fully defined
	//Can instantly init - this means we can also refer to it inside fn (recursion)
	mark_initialized();
	function(TYPE_FUNCTION);
	define_variable(global);
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

static void for_statement(void) {
	//Scope var declaration
	begin_scope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	//Var initializer
	if(match(TOKEN_SEMICOLON)) {
		//No initializer
	} else if(match(TOKEN_VAR)) {
		var_declaration();
	}else {
		//Statement because we need a ; after expression + pop value off stack so it does not leave value on stack
		expression_statement();
	}
	//Cond 
	int loopStart = current_chunk()->size;
	int exitJump = -1;
	if(!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emit_jump(OP_JUMP_IF_FALSE);
		emit_byte(OP_POP); // Condition.
	}

	//Increment clause
	//Jump over increment (if there is one), run body, jump back to increment, run it and go to next iteration
	if(!match(TOKEN_RIGHT_PAREN)) {
		//Jump over increment to body statements
		int bodyJump = emit_jump(OP_JUMP);
		int incrementStart = current_chunk()->size;
		//Increment expression
		expression();
		//Usually assigment, so we only care about side effect, not value on stack -> pop off
		emit_byte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
		//Jump to start of loop (before cond check) right after executing the increment clause
		emit_loop(loopStart);
		//Point loop start to offset where increment expressions begins
		loopStart = incrementStart;
		patch_jump(bodyJump);
	}

	statement();
	//Jump back to cond
	emit_loop(loopStart);
	//Only if cond clause is there
	if (exitJump != -1) {
		patch_jump(exitJump);
		emit_byte(OP_POP); // Condition.
	}

	end_scope();
}

static void if_statement(void) {
	//Compile condition expression (leaves cond value on top of stack)
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	//Emit jump instruction
	//Operand = how much to offset ip if cond is false
	//How many bytes to jump? haven't compiled then branch yet
	//Use backpatching = emit jump instruction with placeholder offset operand
	//Keep track where half finished instruction is
	//Compile then body
	//We know how far to jump
	//Replace placeholder
	int thenJump = emit_jump(OP_JUMP_IF_FALSE);
	//Make sure cond var gets popped off once: in then
	emit_byte(OP_POP);
	statement();
	int elseJump = emit_jump(OP_JUMP);

	patch_jump(thenJump);
	//Make sure cond var gets popped off once: or in else
	emit_byte(OP_POP);

	//Compile else branch if there
	//IF cond = false we jump to else
	//BUT if cond = true we need to execute then branch AND jump over else branch after
	if (match(TOKEN_ELSE)) 
		statement();

	patch_jump(elseJump);
}

static void print_statement(void) {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value");
	emit_byte(OP_PRINT);
}

static void return_statement(void) {
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}

	if(match(TOKEN_SEMICOLON)) {
		emit_return();
	} else {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emit_byte(OP_RETURN);
	}
}

static void while_statement(void) {
	//Location to jump back to if needed
	int loopStart = current_chunk()->size;
	//Parse cond 
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	//Jump over body if cond is false and take care of cond value on stack on either path
	int exitJump = emit_jump(OP_JUMP_IF_FALSE);
	emit_byte(OP_POP);
	statement();
	//Jump back to start instructions again starting with the cond check
	emit_loop(loopStart);
	patch_jump(exitJump);
	emit_byte(OP_POP);
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

	if(match(TOKEN_CLASS)) {
		class_declaration();
	}
	else if(match(TOKEN_FUN)) {
		fun_declaration();
	}
	else if(match(TOKEN_VAR)) {
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
	}
	else if(match(TOKEN_FOR)) {
		for_statement();
	}
	else if (match(TOKEN_IF)) {
		if_statement();
	}
	else if(match(TOKEN_RETURN)) {
		return_statement();
	}
	else if(match(TOKEN_WHILE)) {
		while_statement();
	}
	else if (match(TOKEN_LEFT_BRACE)) {
		begin_scope();
		block();
		end_scope();
	}
	else {
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

//Can be implemented better with specific or instructions
//But implemented with instructions already there
static void or_(bool canAssign) {
	//If left hand = true -> skip right hand expression
	//Jump when value is truthy
	//When left hand = false -> jump over uncondontial jump instruction
	int elseJump = emit_jump(OP_JUMP_IF_FALSE);
	//If left hand = true -> entire expression is true, jump over right hand expression
	int endJump = emit_jump(OP_JUMP);

	patch_jump(elseJump);
	//Pop value off and compile right hand
	emit_byte(OP_POP);
	parse_precedence(PREC_OR);
	patch_jump(endJump);
}

static void named_variable(Token name, bool canAssign) {
	//Check what type of var we need get/set instructions for: global or local
	uint8_t getOp, setOp;
	//Look for local var first
	int arg = resolve_local(current, &name);
	//Local var
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	//Local scope of enclosing functions
	else if((arg = resolve_upvalue(current, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	//Global var
	else {
		arg = identifier_constant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	//Assign expr to var
	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emit_bytes(setOp, (uint8_t)arg);
	}
	//Read var
	else {
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

static void call(bool canAssign) {
	uint8_t argCount = argument_list();
	emit_bytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint8_t name = identifier_constant(&parser.prev);

	if(canAssign && match(TOKEN_EQUAL)) {
		expression();
		emit_bytes(OP_SET_PROPERTY, name);
	} else {
		emit_bytes(OP_GET_PROPERTY, name);
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
  [TOKEN_LEFT_PAREN] =		{grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE] =		{NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT] =				{NULL,     dot,    PREC_CALL},
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
  [TOKEN_AND] =				{NULL,     and_,   PREC_AND},
  [TOKEN_CLASS] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE] =			{NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE] =			{literal,  NULL,   PREC_NONE},
  [TOKEN_FOR] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_IF] =				{NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL] =				{literal,     NULL,   PREC_NONE},
  [TOKEN_OR] =				{NULL,     or_,    PREC_OR},
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

ObjFunction* compile(const char* source) {
	init_scanner(source);
	Compiler compiler; init_compiler(&compiler, TYPE_SCRIPT);
	
	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while(!match(TOKEN_EOF)) {
		declaration();
	}

	ObjFunction* function = end_compiler();
	return parser.hadError ? NULL : function;
}

void mark_compiler_roots(void) {
	Compiler* compiler = current;
	while(compiler != NULL) {
		mark_object((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}