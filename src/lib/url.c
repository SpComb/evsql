
#include "url.h"
#include "lexer.h"

enum url_token {
    URL_INVALID,
    
    URL_BEGIN,

    // kludge to resolve ambiguous URL_SCHEME/URL_USERNAME+URL_PASSWORD/URL_HOSTNAME+URL_SERVICE at the beginning
    URL_BEGIN_ALNUM,
    URL_BEGIN_COLON,

    URL_SCHEME,
    URL_SCHEME_SEP,
    URL_SCHEME_END_COL,
    URL_SCHEME_END_SLASH1,
    URL_SCHEME_END_SLASH2,

    // kludge to resolve ambiguous URL_USERNAME+URL_PASSWORD/URL_HOSTNAME+URL_SERVICE after a scheme 
    URL_USERHOST_ALNUM,
    URL_USERHOST_COLON,
    URL_USERHOST_ALNUM2,
    
    URL_USERNAME,
    URL_PASSWORD_SEP,
    URL_PASSWORD,
    URL_USERNAME_END,

    URL_HOSTNAME,

    URL_SERVICE_SEP,
    URL_SERVICE,

    URL_PATH_START,
    URL_PATH,

    URL_OPT_START,
    URL_OPT_KEY,
    URL_OPT_EQ,
    URL_OPT_VAL,
    URL_OPT_SEP,
    
    URL_END,

    URL_MAX,
};

/*
 * Parser state
 */
struct url_state {
    struct url *url;


};

static int url_lex_token (int _this_token, char *token_data, int _next_token, int _prev_token, void *arg) {
    enum url_token this_token = _this_token, next_token = _next_token, prev_token = _prev_token;
    struct url_state *state = arg;

}

static int url_lex_end (int _last_token, void *arg) {
    enum url_token last_token = _last_token;
    struct url_state *state = arg;

}

static struct lex url_lex = {
    .state_count = URL_MAX,
    .state_list = {
        LEX_STATE ( URL_BEGIN ) {
            LEX_ALNUM       (           URL_BEGIN_ALNUM         ),
            LEX_CHAR        (   ':',    URL_SERVICE_SEP         ),
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },
        
        // this can be URL_SCHEME, URL_USERNAME or URL_HOSTNAME
        LEX_STATE_END ( URL_BEGIN_ALNUM ) {
            LEX_ALNUM       (           URL_BEGIN_ALNUM         ),
            LEX_CHAR        (   '+',    URL_SCHEME_SEP          ),  // it was URL_SCHEME
            LEX_CHAR        (   ':',    URL_BEGIN_COLON         ), 
            LEX_CHAR        (   '@',    URL_USERNAME_END        ),  // it was URL_USERNAME
            LEX_CHAR        (   '/',    URL_PATH_START          ),  // it was URL_HOSTNAME
            LEX_CHAR        (   '?',    URL_OPT_START           ),  // it was URL_HOSTNAME
            LEX_END
        },
        
        // this can be URL_SCHEME_END_COL, URL_USERNAME_END or URL_SERVICE_SEP
        LEX_STATE ( URL_BEGIN_COLON ) {
            LEX_CHAR        (   '/',    URL_SCHEME_END_SLASH1   ),  // it was URL_SCHEME
            LEX_ALNUM       (           URL_USERHOST_ALNUM2     ),
            LEX_END
        },
       

        LEX_STATE ( URL_SCHEME ) { 
            LEX_ALNUM       (           URL_SCHEME              ),
            LEX_CHAR        (   '+',    URL_SCHEME_SEP          ),
            LEX_CHAR        (   ':',    URL_SCHEME_END_COL      ),
            LEX_END
        },

        LEX_STATE ( URL_SCHEME_SEP ) {
            LEX_ALNUM       (           URL_SCHEME              ),
            LEX_END
        },

        LEX_STATE ( URL_SCHEME_END_COL ) {
            LEX_CHAR        (   '/',    URL_SCHEME_END_SLASH1   ),
            LEX_END
        },

        LEX_STATE ( URL_SCHEME_END_SLASH1 ) {
            LEX_CHAR        (   '/',    URL_SCHEME_END_SLASH2   ),
            LEX_END
        },

        LEX_STATE_END ( URL_SCHEME_END_SLASH2 ) {
            LEX_ALNUM       (           URL_USERHOST_ALNUM      ),
            LEX_CHAR        (   ':',    URL_SERVICE_SEP         ),
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },
        
        // this can be URL_USERNAME or URL_HOSTNAME
        LEX_STATE_END ( URL_USERHOST_ALNUM ) {
            LEX_ALNUM       (           URL_USERHOST_ALNUM      ),
            LEX_CHAR        (   ':',    URL_USERHOST_COLON      ), 
            LEX_CHAR        (   '@',    URL_USERNAME_END        ),  // it was URL_USERNAME
            LEX_CHAR        (   '/',    URL_PATH_START          ),  // it was URL_HOSTNAME
            LEX_CHAR        (   '?',    URL_OPT_START           ),  // it was URL_HOSTNAME
            LEX_END
        }
        
        // this can be URL_USERNAME_END or URL_SERVICE_SEP
        LEX_STATE ( URL_USERHOST_COLON ) {
            LEX_ALNUM       (           URL_USERHOST_ALNUM2        ),
            LEX_END
        },
        
        // this can be URL_PASSWORD or URL_SERVICE
        LEX_STATE_END ( URL_USERHOST_ALNUM2 ) {
            LEX_ALNUM       (           URL_USERHOST_ALNUM      ),
            LEX_CHAR        (   '@',    URL_USERNAME_END        ),  // it was URL_PASSSWORD
            LEX_CHAR        (   '/',    URL_PATH_START          ),  // it was URL_SERVICE
            LEX_CHAR        (   '?',    URL_OPT_START           ),  // it was URL_SERVICE
            LEX_END
        },
        
        // dummy states, covered by URL_USERHOST_ALNUM/URL_USERHOST_COLON/URL_USERHOST_ALNUM2
        LEX_STATE ( URL_USERNAME ) {
            LEX_END
        },

        LEX_STATE ( URL_PASSWORD_SEP ) {
            LEX_END
        },

        LEX_STATE ( URL_PASSWORD ) {
            LEX_END
        },


        LEX_STATE_END ( URL_USERNAME_END ) {
            LEX_ALNUM       (           URL_HOSTNAME            ), 
            LEX_CHAR        (   ':',    URL_SERVICE_SEP         ),
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },


        LEX_STATE_END ( URL_HOSTNAME ) {
            LEX_ALNUM       (           URL_HOSTNAME            ), 
            LEX_CHAR        (   ':',    URL_SERVICE_SEP         ),
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },


        LEX_STATE ( URL_SERVICE_SEP ) {
            LEX_ALNUM       (           URL_SERVICE            ), 
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },

        LEX_STATE_END ( URL_SERVICE ) {
            LEX_ALNUM       (           URL_SERVICE            ), 
            LEX_CHAR        (   '/',    URL_PATH_START          ),
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_END
        },


        LEX_STATE_END ( URL_PATH_START ) {
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_DEFAULT     (           URL_PATH                ),
        },

        LEX_STATE_END ( URL_PATH ) {
            LEX_CHAR        (   '?',    URL_OPT_START           ),
            LEX_DEFAULT     (           URL_PATH                ),
        },


        LEX_STATE_END ( URL_OPT_START ) {
            LEX_CHAR        (   '&',    URL_OPT_SEP             ),
            LEX_CHAR        (   '=',    URL_ERROR               ),
            LEX_DEFAULT     (           URL_OPT_KEY             ),
        },

        LEX_STATE_END ( URL_OPT_KEY ) {
            LEX_CHAR        (   '&',    URL_OPT_SEP             ),
            LEX_CHAR        (   '=',    URL_OPT_EQ              ),
            LEX_DEFAULT     (           URL_OPT_KEY             ),
        },

        LEX_STATE_END ( URL_OPT_EQ ) {
            LEX_CHAR        (   '&',    URL_OPT_SEP             ),
            LEX_DEFAULT     (           URL_OPT_VAL             ),
        },

        LEX_STATE_END ( URL_OPT_VAL ) {
            LEX_CHAR        (   '&',    URL_OPT_SEP             ),
            LEX_DEFAULT     (           URL_OPT_VAL             ),
        },

        LEX_STATE_END ( URL_OPT_SEP ) {
            LEX_CHAR        (   '&',    URL_OPT_SEP             ),
            LEX_CHAR        (   '=',    URL_ERROR               ),
            LEX_DEFAULT     (           URL_OPT_KEY             ),
        },
        
        LEX_STATE ( URL_ERROR ) {
            LEX_END
        },

        URL_MAX,
    },

    .token_fn = url_lex_token,
    .char_fn = NULL,
    .end_fn = url_lex_end,
};

int url_parse (struct url *url, const char *text) {
    struct url_state state; ZINIT(state);
    int ret;

    // set up state
    state.url = url;
    
    // parse it
    if ((ret = lexer(&url_lex, text, &state)))
        ERROR("invalid URL");

    // success
    return 0;

error:
    return -1;
}

