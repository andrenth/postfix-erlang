#include "sys_defs.h"

#ifdef HAS_ERLANG
#include <sys/types.h>

#include <err.h>
#include <string.h>
#include <unistd.h>

#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif

#include "argv.h"
#include "cfg_parser.h"
#include "db_common.h"
#include "dict.h"
#include "msg.h"
#include "mymalloc.h"
#include "stringops.h"
#include "vstring.h"

#include "erl_interface.h"
#include "ei.h"

#include "dict_erlang.h"

typedef struct {
    DICT dict;
    CFG_PARSER *parser;
    void *ctx;
    char *node;
    char *cookie;
    char *mod;
    char *fun;
} DICT_ERLANG;

struct query_data {
    int fd;
    char *ret;
    ei_x_buff msg;
    ei_x_buff res;
};

static void
msg_warn_erl(char *s)
{
    msg_warn("%s: %s", s, strerror(erl_errno));
}

static void
query_cleanup(struct query_data *qdp, char *warn)
{
    if (warn != NULL)
        msg_warn_erl(warn);
    if (qdp->fd != -1)
        close(qdp->fd);
    if (qdp->ret != NULL)
        myfree(qdp->ret);
    ei_x_free(&qdp->msg);
    ei_x_free(&qdp->res);
}

static int
alloc_buffer(struct query_data *qdp, int *index)
{
    int err;
    int type, size;

    err = ei_get_type(qdp->res.buff, index, &type, &size);
    if (err < 0) {
        query_cleanup(qdp, "cannot get term size");
        return err;
    }
    qdp->ret = mymalloc(size + 1);
    if (qdp->ret == NULL) {
        query_cleanup(qdp, "cannot allocate buffer");
        return -1;
    }
    return size;
}

static int
decode_atom(struct query_data *qdp, int *index, const char *cmp)
{
    int err;
    int size;

    size = alloc_buffer(qdp, index);
    if (size < 0)
        return size;
    err = ei_decode_atom(qdp->res.buff, index, qdp->ret);
    if (err != 0) {
        query_cleanup(qdp, "cannot decode atom");
        return err;
    }
    qdp->ret[size] = '\0';
    if (strcmp(qdp->ret, cmp) != 0) {
        char *warn = mymalloc(size+10);
        snprintf(warn, size+10, "bad atom: %s", qdp->ret);
        query_cleanup(qdp, warn);
        myfree(warn);
        return -1;
    }
    return 0;
}

static int
decode_tuple(struct query_data *qdp, int *index, int num_elements)
{
    int err;
    int arity;

    err = ei_decode_tuple_header(qdp->res.buff, index, &arity);
    if (err < 0) {
        query_cleanup(qdp, "cannot decode response tuple");
        return err;
    }
    if (arity != num_elements) {
        query_cleanup(qdp, "bad response tuple arity");
        return -1;
    }
    return 0;
}


static int
decode_bitstring(struct query_data *qdp, int *index)
{
    int err;
    int size;
    long len;

    size = alloc_buffer(qdp, index);
    if (size < 0)
        return size;
    err = ei_decode_binary(qdp->res.buff, index, qdp->ret, &len);
    if (err < 0) {
        query_cleanup(qdp, "cannot decode response bitstring");
        return err;
    }
    qdp->ret[size] = '\0';
    return 0;
}

static int
erlang_query(DICT_ERLANG *dict_erlang, const char *key, char *node,
             char *cookie, char *mod, char *fun, char **ret)
{
    int err, index;
    int res_version, res_type, res_size;
    char *r;
    struct query_data qd;
    ei_cnode ec;

    memset(&qd, 0, sizeof(qd));
    qd.fd = -1;
    *ret = NULL;

    err = ei_connect_init(&ec, "dict_erlang", cookie, 0);
    if (err != 0) {
        query_cleanup(&qd, "ei_connect_init");
        return err;
    }

    qd.fd = ei_connect(&ec, node);
    if (qd.fd < 0) {
        query_cleanup(&qd, "ei_connect");
        return -1;
    }

    ei_x_new(&qd.msg);
    ei_x_new(&qd.res);

    ei_x_encode_list_header(&qd.msg, 1);
    ei_x_encode_binary(&qd.msg, key, strlen(key));
    ei_x_encode_empty_list(&qd.msg);

    err = ei_rpc(&ec, qd.fd, mod, fun, qd.msg.buff, qd.msg.index, &qd.res);
    if (err == -1) {
        query_cleanup(&qd, "ei_rpc");
        return err;
    }

    index = 0;
    err = ei_get_type(qd.res.buff, &index, &res_type, &res_size);
    if (err != 0) {
        query_cleanup(&qd, "ei_get_type");
        return err;
    }

    switch (res_type) {
    case ERL_ATOM_EXT:
        err = decode_atom(&qd, &index, "not_found");
        if (err == -1)
            return err;
        query_cleanup(&qd, NULL);
        return 0;
    case ERL_SMALL_TUPLE_EXT:
    case ERL_LARGE_TUPLE_EXT: {
        int arity;
        long len;
        err = decode_tuple(&qd, &index, 2);
        if (err == -1)
            return err;
        err = decode_atom(&qd, &index, "ok");
        if (err == -1)
            return err;
        err = decode_bitstring(&qd, &index);
        if (err == -1)
            return err;
        *ret = mystrdup(qd.ret);
        query_cleanup(&qd, NULL);
        return 1;
    }
    default:
        query_cleanup(&qd, "unexpected response type");
        return -1;
    }

    /* NOTREACHED */
}

static const char *
dict_erlang_lookup(DICT *dict, const char *key)
{
    int ret;
    char *res;
    const char *r;
    static VSTRING *val;
    const char *myname = "dict_erlang_lookup";
    DICT_ERLANG *dict_erlang = (DICT_ERLANG *)dict;

    dict_errno = 0;

    if (dict->flags & DICT_FLAG_FOLD_FIX) {
        if (dict->fold_buf == 0)
            dict->fold_buf = vstring_alloc(10);
        vstring_strcpy(dict->fold_buf, key);
        key = lowercase(vstring_str(dict->fold_buf));
    }

    if (db_common_check_domain(dict_erlang->ctx, key) == 0) {
        if (msg_verbose)
            msg_info("%s: Skipping lookup of '%s'", myname, key);
        return 0;
    }

#define INIT_VSTR(buf, len) do { \
    if (buf == 0) \
        buf = vstring_alloc(len); \
    VSTRING_RESET(buf); \
    VSTRING_TERMINATE(buf); \
} while (0)

    INIT_VSTR(val, 10);

    ret = erlang_query(dict_erlang, key, dict_erlang->node,
                       dict_erlang->cookie, dict_erlang->mod,
                       dict_erlang->fun, &res);
    switch (ret) {
    case -1:
        dict_errno = DICT_ERR_RETRY;
        return NULL;
    case 0:
        /* Not found */
        return NULL;
    case 1:
        vstring_strcpy(val, res);
        myfree(res);
        r = vstring_str(val);
        return ((dict_errno == 0 && *r) ? r : NULL);
    default:
        msg_warn("unexpected query result");
        return NULL;
    }
}

static void
erlang_parse_config(DICT_ERLANG *dict_erlang, const char *erlangcf)
{
    CFG_PARSER *p;

    p = dict_erlang->parser = cfg_parser_alloc(erlangcf);
    dict_erlang->node = cfg_get_str(p, "node", "", 3, 0);
    dict_erlang->cookie = cfg_get_str(p, "cookie", "", 1, 0);
    dict_erlang->mod = cfg_get_str(p, "module", "", 1, 0);
    dict_erlang->fun = cfg_get_str(p, "function", "", 1, 0);

    dict_erlang->ctx = NULL;
    db_common_parse(&dict_erlang->dict, &dict_erlang->ctx, "%s", 1);
    db_common_parse_domain(p, dict_erlang->ctx);

    if (db_common_dict_partial(dict_erlang->ctx))
        dict_erlang->dict.flags |= DICT_FLAG_PATTERN;
    else
        dict_erlang->dict.flags |= DICT_FLAG_FIXED;
    if (dict_erlang->dict.flags & DICT_FLAG_FOLD_FIX)
        dict_erlang->dict.fold_buf = vstring_alloc(10);
}

static void
dict_erlang_close(DICT *dict)
{
    DICT_ERLANG *dict_erlang = (DICT_ERLANG *)dict;

    cfg_parser_free(dict_erlang->parser);
    myfree(dict_erlang->node);
    myfree(dict_erlang->cookie);
    myfree(dict_erlang->mod);
    myfree(dict_erlang->fun);
    dict_free(dict);
}


DICT *
dict_erlang_open(const char *key, int open_flags, int dict_flags)
{
    DICT_ERLANG *dict_erlang;

    if (open_flags != O_RDONLY)
        msg_fatal("%s:%s map requires O_RDONLY access mode",
                  DICT_TYPE_ERLANG, key);

    erl_init(NULL, 0);

    dict_erlang = (DICT_ERLANG *)dict_alloc(DICT_TYPE_ERLANG, key,
                                            sizeof(DICT_ERLANG));
    dict_erlang->dict.lookup = dict_erlang_lookup;
    dict_erlang->dict.close = dict_erlang_close;
    dict_erlang->dict.flags = dict_flags;
    erlang_parse_config(dict_erlang, key);

    return (DICT_DEBUG(&dict_erlang->dict));
}

#endif
