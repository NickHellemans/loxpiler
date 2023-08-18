#include "scanner.h"

#include <string.h>

#include "common.h"

typedef struct {
	//Points to start of token being scanned
	const char* start;
	//Point to current char being looked at
	const char* curr;
	int line;
} Scanner;

//Create global value to not have to pass it around
Scanner scanner;

void init_scanner(const char* source) {
	scanner.start = source;
	scanner.curr = source;
	scanner.line = 1;
}

static bool is_alpha(char c) {
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_';
}

static bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

static bool is_at_end(void) {
	return *scanner.curr == '\0';
}

static char advance(void) {
	scanner.curr++;
	return scanner.curr[-1];
}

//Consumes token if match
static bool match(char expected) {
	if (is_at_end())
		return false;

	if (*scanner.curr != expected)
		return false;

	scanner.curr++;
	return true;
}

Token make_token(TokenType type) {
	Token token;
	token.type = type;
	token.start = scanner.start;
	token.length = (int)(scanner.curr - scanner.start);
	token.line = scanner.line;
	return token;
}

static Token error_token(const char* message) {
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner.line;
	return token;
}

static char peek(void) {
	return *scanner.curr;
}

static char peek_next(void) {
	if (is_at_end()) 
		return '\0';
	return scanner.curr[1];
}

static void skip_whitespace(void) {
	for(;;) {
		char c = peek();
		switch (c) {
			case ' ':
			case '\r':
			case '\t':
				advance();
				break;

			case '\n':
				scanner.line++;
				advance();
				break;

			case '/':
				if(peek_next() == '/') {
					while (peek() != '\n' && !is_at_end())
						advance();
				} else {
					return;
				}
				break;

			default:
				return;
		}
	}
}

//Check for keyword
// - Check lengths
// - Check chars
static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
		//Check length of current identifier and possible keyword
	if(scanner.curr - scanner.start == start + length
		//Check if chars are the same
		&& memcmp(scanner.start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

//Minimum amount of work required to detect a keyword using a trie 
static TokenType identifier_type(void) {
	switch (scanner.start[0]) {
		case 'a': return check_keyword(1, 2, "nd", TOKEN_AND);
		case 'c': return check_keyword(1, 4, "lass", TOKEN_CLASS);
		case 'e': return check_keyword(1, 3, "lse", TOKEN_ELSE);
		case 'f':
			//for, false or fun
			//Check if there even is another letter
			if (scanner.curr - scanner.start > 1) {
				switch (scanner.start[1]) {
					case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
					case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
					case 'u': return check_keyword(2, 1, "n", TOKEN_FUN);
				}
			}
			break;
		case 'i': return check_keyword(1, 1, "f", TOKEN_IF);
		case 'n': return check_keyword(1, 2, "il", TOKEN_NIL);
		case 'o': return check_keyword(1, 1, "r", TOKEN_OR);
		case 'p': return check_keyword(1, 4, "rint", TOKEN_PRINT);
		case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
		case 's': return check_keyword(1, 4, "uper", TOKEN_SUPER);
		case 't':
			//true, this
			//Check if there even is another letter
			if (scanner.curr - scanner.start > 1) {
				switch (scanner.start[1]) {
					case 'h': return check_keyword(2, 2, "is", TOKEN_THIS);
					case 'r': return check_keyword(2, 2, "ue", TOKEN_TRUE);
				}
			}
			break;
		case 'v': return check_keyword(1, 2, "ar", TOKEN_VAR);
		case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
	while (is_alpha(peek()) || is_digit(peek())) 
		advance();
	return make_token(identifier_type());
}

static Token number(void) {
	while (is_digit(peek())) 
		advance();

	// Look for a fractional part.
	if (peek() == '.' && is_digit(peek_next())) {
		// Consume the ".".
		advance();

		while (is_digit(peek())) advance();
	}

	return make_token(TOKEN_NUMBER);
}

static Token string(void) {
	while (peek() != '"' && !is_at_end()) {
		if (peek() == '\n') 
			scanner.line++;

		advance();
	}

	if (is_at_end()) return error_token("Unterminated string.");

	// The closing quote.
	advance();
	return make_token(TOKEN_STRING);
}

Token scan_token(void) {

	skip_whitespace();
	scanner.start = scanner.curr;

	if (is_at_end())
		return make_token(TOKEN_EOF);

	char c = advance();

	if (is_alpha(c)) return identifier();

	if (is_digit(c))
		return number();

	switch (c) {
		case '(': return make_token(TOKEN_LEFT_PAREN);
		case ')': return make_token(TOKEN_RIGHT_PAREN);
		case '{': return make_token(TOKEN_LEFT_BRACE);
		case '}': return make_token(TOKEN_RIGHT_BRACE);
		case ';': return make_token(TOKEN_SEMICOLON);
		case ',': return make_token(TOKEN_COMMA);
		case '.': return make_token(TOKEN_DOT);
		case '-': return make_token(TOKEN_MINUS);
		case '+': return make_token(TOKEN_PLUS);
		case '/': return make_token(TOKEN_SLASH);
		case '*': return make_token(TOKEN_STAR);
		case '!':
			return make_token(
				match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
		case '=':
			return make_token(
				match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
		case '<':
			return make_token(
				match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
		case '>':
			return make_token(
				match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

		case '"': return string();
	}

	return error_token("Unexpected character.");
}