#ifndef LR1_TOKEN_H
#define LR1_TOKEN_H

enum {
	NON_TERMINAL_START,
	EXPRRESSION, // E
	TERM, // T
	FACTOR, // F
	ACCEPTED, //accept

	END, //$ temporary

	S,  // shift
	R, // reducce
	G, //goto
	ACC, //acc
};

#endif