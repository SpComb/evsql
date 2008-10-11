
#include <stdlib.h>

#include "lex.h"
#include "error.h"
#include "log.h"

#define INITIAL_BUF_SIZE 4096

int lexer (const struct lex *lex, const char *input, void *arg) {
    // handling error returns
    int err = -1, cb_err;
    
    // token buffer
    char *buf = NULL, *buf_ptr;
    size_t buf_size = INITIAL_BUF_SIZE;
    
    // state
    int prev_state = LEX_INITIAL, cur_state = lex->initial_state, next_state = LEX_INITIAL;
    
    // input chars
    const char *c = input;

    // lookups
    const struct lex_transition *trans = NULL;

    // allocate the buffer
    if ((buf = malloc(sizeof(char) * buf_size)) == NULL)
        goto error;

    // set buf_ptr initial position
    buf_ptr = buf;
    
    // clear input
    DEBUG("*cough*");
    DEBUGN("%s", "");

    // process input
    do {
        if (*c) {
            // look up the next state
            for (trans = lex->state_list[cur_state - 1].trans_list; trans->next_state > 0 || trans->flags; trans++) {
                // accept defaults
                if (trans->flags & LEX_TRANS_DEFAULT)
                    break;
                
                // disregard non-matches
                if (trans->left > *c || *c > trans->right)
                    continue;
                
                // abort on invalids
                if (trans->flags & LEX_TRANS_INVALID) {
                    goto error;
                
                } else {
                    // accept it
                    break;
                }
            }
            
            // did we find a transition with a valid next state?
            if (!(next_state = trans->next_state))
                goto error;

            // call the char handler
            if (lex->char_fn && (cb_err = lex->char_fn(*c, cur_state, next_state, arg)))
                goto error;

        } else {
            // EOF!
            next_state = LEX_EOF;
            
            // is cur_state a valid end state?
            if (!(lex->state_list[cur_state - 1].flags & LEX_STATE_END))
                goto error;
            
            // note: we don't pass the NUL byte to the char handler
        }

        // if this char is part of the next token...
        if (next_state != cur_state) {
            // terminate the buffer and reset buf_ptr
            *buf_ptr = 0; buf_ptr = buf;
            
            // dump state transitions
            DEBUGF("\n\t%25s -> %25s -> %25s",
                LEX_STATE_NAME(lex, prev_state),
                LEX_STATE_NAME(lex, cur_state),
                LEX_STATE_NAME(lex, next_state)
            );

            // pass in the complete token to the handler
            if (lex->token_fn && (cb_err = lex->token_fn(cur_state, buf, next_state, prev_state, arg)))
                goto error;

            // update states
            prev_state = cur_state;
            cur_state = next_state;
            next_state = LEX_INITIAL;
        }
        
        // dump chars
        if (next_state == LEX_INITIAL)
            DEBUGN("%c", *c);
        else
            DEBUGNF("%c", *c);
        
        // store this char in the buffer
        *(buf_ptr++) = *c;

        // grow the buffer if needed
        if (buf_ptr - buf >= buf_size) {
            // remember the offset, as buf_ptr might get invalidated if buf is moved
            size_t buf_offset = buf_ptr - buf;

            // calc new size
            buf_size *= 2;
            
            // grow/move
            if ((buf = realloc(buf, buf_size)) == NULL)
                goto error;
            
            // fix buf_ptr
            buf_ptr = buf + buf_offset;
        }
    } while (*(c++));

    // call the end handler
    if (lex->end_fn && (cb_err = lex->end_fn(cur_state, arg)))
        goto error;

    // successfully parsed!
    err = 0;

error:
    DEBUGNF("\n");
    
    if (cb_err)
        err = cb_err;

    // dump debug info on error
    if (err) {
        const char *cc;
        
        // figure out the error
        if (!buf)
            WARNING("malloc/realloc");

        else if (trans && trans->flags & LEX_TRANS_INVALID)
            WARNING("hit invalid transition match");

        else if (!next_state)
            WARNING("no valid transition found");
            
        else if (next_state == LEX_EOF && !(lex->state_list[cur_state - 1].flags & LEX_STATE_END))
            WARNING("invalid end state");
        
        else
            WARNING("unknown error condition (!?)");

        DEBUG("%s", input);
        DEBUGN("%s", "");

        for (cc = input; cc < c; cc++)
            DEBUGNF(" ");

        DEBUGF("^\t%s -> %s -> %s",
            LEX_STATE_NAME(lex, prev_state),
            LEX_STATE_NAME(lex, cur_state),
            LEX_STATE_NAME(lex, next_state)
        );
    }

    // free stuff
    free(buf);

    // return
    return err;
}


