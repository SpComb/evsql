#ifndef LIB_LEXER_H
#define LIB_LEXER_H

/*
 * Simple FSM lexing
 *
 * The lexer is implemented as a Finite State Machine, consisting for a number of states, which then contain a set of
 * transitions, which move the lexer from state to state based on each char of input at a time.
 *
 * Whenever the state changes, the token callback is triggered with the collected token data.
 */

#include <sys/types.h>

/*
 * Transition flags
 */
enum lex_transition_flags {
    LEX_TRANS_DEFAULT   = 0x01,
    LEX_TRANS_FINAL     = 0x02,
    LEX_TRANS_INVALID   = 0x04,
};

/*
 * A transition from one state to another.
 */
struct lex_transition {
    // applies to chars [left, right]
    char left, right;
    
    // flags from lex_transition_flags
    char flags;
    
    // next state to enter
    int next_state;
};

/*
 * State flags
 */ 
enum lex_state_flags {
    LEX_STATE_END       = 0x01,
};

/*
 * A state
 */
struct lex_state {
    // the state name (for debugging)
    const char *name;

    // flags from lex_state_flags
    char flags;

    // list of transitions for this state, terminated by a transition with next_state=0
    struct lex_transition trans_list[15];
};

/*
 * Special tokens
 */

// shows up in token_fn as the value of next_token when this_token is the last token.
#define LEX_EOF 0

/*
 * Lex machine
 */
struct lex {
    /*
     * Core token handler. Everytime a full token is lexed (i.e. the state changes), this will be called.
     * `this_token` represents the full token that was parsed, and `token_data` is the token's value. `next_token`
     * is the state that terminated this token, and `prev_token` was the token before this one.
     *
     * `token_data` is a buffer allocated by the lexer that the actual input data is copied into. Thence, it can be
     * modified, as its contents will be replaced by the next token. Hence, if you need to keep hold of it, copy it.
     *
     * Return zero to have lexing continue, nonzero to stop lexing.
     */
    int (*token_fn) (int this_token, char *token_data, int next_token, int prev_token, void *arg);

    /*
     * Called on every char handled by the lexer. `this_token` is the state of the token that the char belongs to.
     *
     * Return zero to have lexing continue, nonzero to stop lexing.
     */
    int (*char_fn) (int this_token, char token_char, void *arg);

    /*
     * Called when the end of input has been reached, `last_token` is the state that we terminated in.
     *
     * Return zero to indiciate that the input was valid, nonzero to indicate an error.
     */
    int (*end_fn) (int last_token, void *arg);
    
    // number of states
    size_t state_count;

    // array of lex_states, indexable by the state id.
    struct lex_state state_list[];
};

/*
 * Helper macros for building the state_list
 */
#define LEX_STATE(enum_val)     { #enum_val, 0,
#define LEX_STATE_END(enum_val) { #enum_val, LEX_STATE_END,

    #define LEX_CHAR(c, to)         { c, c, 0, to }
    #define LEX_RANGE(l, r, to)     { l, r, 0, to }
    #define LEX_ALPHA(to)           LEX_RANGE('a', 'z', to), LEX_RANGE('A', 'Z', to)
    #define LEX_NUMBER(to)          LEX_RANGE('0', '9', to)
    #define LEX_ALNUM(to)           LEX_ALPHA(to), LEX_NUMBER(to), LEX_CHAR('-', to), LEX_CHAR('_', to)
    #define LEX_WHITESPACE(to)      LEX_CHAR(' ', to), LEX_CHAR('\n', to), LEX_CHAR('\t', to)
    #define LEX_INVALID(c)          { c, c, LEX_TRANS_INVALID, 0 }

    #define LEX_DEFAULT(to)         { 0, 0, LEX_TRANS_DEFAULT, to } \
                                  }
    #define LEX_END                 { 0, 0, 0, 0 } \
                                  }

/*
 * Lex it!
 *
 * Return zero to indiciate that the input was valid, nonzero otherwise.
 */
int lexer (const struct lex *lex, const char *input, void *arg);

#endif /* LIB_LEXER_H */
