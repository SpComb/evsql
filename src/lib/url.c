
#include "url.h"
#include "lexer.h"

enum url_tokens {
    URL_INVALID,
    
    URL_SCHEME,
    URL_SCHEME_SEP,
    URL_SCHEME_END_COL,
    URL_SCHEME_END_SLASH1,
    URL_SCHEME_END_SLASH2,
    
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

    URL_MAX,
};

static struct lex *url_lex = {
    .state_count = URL_MAX,
    .stae_list = {
        LEX_STATE(URL_SCHEME)
            LEX_ALNUM       (           URL_SCHEME          ),
            LEX_CHAR        (   '+',    URL_SCHEME_SEP      ),
        LEX_STATE_END,



    },
}

