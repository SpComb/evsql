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

/*
 * A transition from one state to another.
 */
struct lex_transition {
    // applies to chars [left, right]
    char left, right;
    
    // next state to enter
    int next_state;
};

/*
 * A state
 */
struct lex_state {
    // the state name (for debugging)
    const char *name;

    // list of transitions for this state, terminated by a transition with next_state=0
    struct lex_transition *trans_list;


};

/*
 * Lex machine
 */
struct lex {
    // number of states
    size_t state_count;

    // array of lex_states, indexable by the state id.
    struct lex_state *state_list;

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
    int (*lex_token_fn) (int this_token, char *token_data, int next_token, int prev_token, void *arg);

    /*
     * Called on every char handled by the lexer. `this_token` is the state of the token that the char belongs to.
     *
     * Return zero to have lexing continue, nonzero to stop lexing.
     */
    int (*lex_char_fn) (int this_token, char token_char, void *arg);

    /*
     * Called when the end of input has been reached, `last_token` is the state that we terminated in.
     *
     * Return zero to indiciate that the input was valid, nonzero to indicate an error.
     */
    int (*lex_end_fn) (int last_token, void *arg);
};

/*
 * Helper macros for building the state_list
 */
#define LEX_STATE(enum_val)     { #enum_val, {

    #define LEX_CHAR(c, to)         { c, c, to },
    #define LEX_RANGE(l, r, to)     { l, r, to },
    #define LEX_ALPHA(to)           LEX_RANGE('a', 'z', to), LEX_RANGE('A', 'Z', to)
    #define LEX_NUMBER(to)          LEX_RANGE('0', '9', to)
    #define LEX_ALNUM(to)           LEX_ALPHA(to), LEX_NUMBER(to), LEX_CHAR('-', to), LEX_CHAR('_', to)
    #define LEX_WHITESPACE(to)      LEX_CHAR(' ', to), LEX_CHAR('\n', to), LEX_CHAR('\t', to)

#define LEX_STATE_END               {0, 0, 0} \
                                } }

/*
 * Lex it!
 *
 * Return zero to indiciate that the input was valid, nonzero otherwise.
 */
int lexer (struct lex *lex, const char *input, void *arg);

#endif /* LIB_LEXER_H */
