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
    ARGV *nodes;
    char *cookie;
    char *mod;
    char *fun;
    int active_node;
} DICT_ERLANG;

static void
msg_warn_erl(char *s)
{
    msg_warn("%s: %s", s, strerror(erl_errno));
}

static char *
alloc_buffer(ei_x_buff *eip, int *index, int *sizep)
{
    int err;
    int type, size;

    err = ei_get_type(eip->buff, index, &type, &size);
    if (err < 0) {
        msg_warn("cannot get term size");
        return NULL;
    }
    *sizep = size + 1;
    return mymalloc(*sizep);
}

static int
decode_atom(ei_x_buff *eip, int *index, const char *cmp)
{
    int err;
    int size;
    int ret;
    char *buf;

    buf = alloc_buffer(eip, index, &size);
    err = ei_decode_atom(eip->buff, index, buf);
    if (err != 0) {
        msg_warn("cannot decode atom");
        return err;
    }
    buf[size-1] = '\0';

    ret = strcmp(buf, cmp);
    if (ret != 0)
        msg_warn("bad atom: %s", buf);

    myfree(buf);
    return ret;
}

static int
decode_tuple(ei_x_buff *eip, int *index, int num_elements)
{
    int err;
    int arity;

    err = ei_decode_tuple_header(eip->buff, index, &arity);
    if (err < 0) {
        msg_warn("cannot decode response tuple");
        return err;
    }
    if (arity != num_elements) {
        msg_warn("bad response tuple arity");
        return -1;
    }
    return 0;
}


static char *
decode_bitstring(ei_x_buff *eip, int *index)
{
    int err;
    int size;
    long len;
    char *buf;

    buf = alloc_buffer(eip, index, &size);
    err = ei_decode_binary(eip->buff, index, buf, &len);
    if (err < 0) {
        msg_warn("cannot decode response bitstring");
        return NULL;
    }
    buf[size-1] = '\0';
    return buf;
}

static int
decode_list(ei_x_buff *eip, int *index)
{
    int err;
    int arity;

    err = ei_decode_list_header(eip->buff, index, &arity);
    if (err < 0) {
        msg_warn("cannot decode response list");
        return err;
    }
    return arity;
}

static char *
decode_bitstring_list(DICT_ERLANG *dict_erlang, const char *key,
                      ei_x_buff *eip, int *index)
{
    int i;
    int arity;
    static VSTRING *result;

    arity = decode_list(eip, index);
    if (arity < 0)
        return NULL;
    if (arity == 0) {
        msg_warn("found alias with no destinations");
        return NULL;
    }

#define INIT_VSTR(buf, len)           \
    do {                              \
        if (buf == 0)                 \
            buf = vstring_alloc(len); \
        VSTRING_RESET(buf);           \
        VSTRING_TERMINATE(buf);       \
    } while (0)

    INIT_VSTR(result, 10);

    for (i = 0; i < arity; i++) {
        char *s = decode_bitstring(eip, index);
        if (s == NULL)
            return NULL;
        db_common_expand(dict_erlang->ctx, "%s", s, key, result, NULL);
        myfree(s);
    }
    return vstring_str(result);
}

static int
handle_response(DICT_ERLANG *dict_erlang, const char *key,
                ei_x_buff *resp, char **res)
{
    int err, index = 0;
    int res_type, res_size;

    err = ei_get_type(resp->buff, &index, &res_type, &res_size);
    if (err != 0) {
        msg_warn_erl("ei_get_type");
        return err;
    }

    switch (res_type) {
    case ERL_ATOM_EXT:
        err = decode_atom(resp, &index, "not_found");
        if (err != 0)
            return err;
        return 0;
    case ERL_SMALL_TUPLE_EXT:
    case ERL_LARGE_TUPLE_EXT: {
        int arity;
        err = decode_tuple(resp, &index, 2);
        if (err == -1)
            return err;
        err = decode_atom(resp, &index, "ok");
        if (err != 0)
            return err;
        *res = decode_bitstring_list(dict_erlang, key, resp, &index);
        if (*res == NULL)
            return -1;
        return 1;
    }
    default:
        msg_warn("unexpected response type");
        return -1;
    }

    /* NOTREACHED */
}

static int
erlang_query(DICT_ERLANG *dict_erlang, const char *key, ARGV *nodes,
             char *cookie, char *mod, char *fun, char **res)
{
    int cur_node, last_node;
    int err, index;
    int res_version, res_type, res_size;
    int fd;
    ei_cnode ec;
    ei_x_buff args;
    ei_x_buff resp;

    err = ei_connect_init(&ec, "dict_erlang", cookie, 0);
    if (err != 0) {
        msg_warn_erl("ei_connect_init");
        return -1;
    }

    cur_node = dict_erlang->active_node;
    last_node = (cur_node - 1) % nodes->argc;
    do {
        fd = ei_connect(&ec, nodes->argv[cur_node]);
        if (fd >= 0) {
            dict_erlang->active_node = cur_node;
            if (msg_verbose)
                msg_info("connected to node %s", nodes->argv[cur_node]);
            break;
        }
        cur_node = (cur_node + 1) % nodes->argc;
    } while (cur_node != dict_erlang->active_node);

    if (fd < 0) {
        msg_warn_erl("ei_connect");
        return -1;
    }

    ei_x_new(&args);
    ei_x_new(&resp);

    ei_x_encode_list_header(&args, 1);
    ei_x_encode_binary(&args, key, strlen(key));
    ei_x_encode_empty_list(&args);

    err = ei_rpc(&ec, fd, mod, fun, args.buff, args.index, &resp);
    if (err == -1) {
        msg_warn_erl("ei_rpc");
        goto cleanup;
    }

    err = handle_response(dict_erlang, key, &resp, res);

cleanup:
    close(fd);
    ei_x_free(&args);
    ei_x_free(&resp);
    return err;
}

static const char *
dict_erlang_lookup(DICT *dict, const char *key)
{
    int ret;
    char *res;
    const char *myname = "dict_erlang_lookup";
    DICT_ERLANG *dict_erlang = (DICT_ERLANG *)dict;

    dict_errno = 0;

    if (dict->flags & DICT_FLAG_FOLD_FIX) {
        if (dict->fold_buf == NULL)
            dict->fold_buf = vstring_alloc(10);
        vstring_strcpy(dict->fold_buf, key);
        key = lowercase(vstring_str(dict->fold_buf));
    }

    if (db_common_check_domain(dict_erlang->ctx, key) == 0) {
        if (msg_verbose)
            msg_info("%s: Skipping lookup of '%s'", myname, key);
        return 0;
    }

    ret = erlang_query(dict_erlang, key, dict_erlang->nodes,
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
        return res;
    default:
        msg_warn("unexpected query result");
        return NULL;
    }
}

static void
erlang_parse_config(DICT_ERLANG *dict_erlang, const char *erlangcf)
{
    CFG_PARSER *p;
    char *nodes;

    p = dict_erlang->parser = cfg_parser_alloc(erlangcf);

    nodes = cfg_get_str(p, "nodes", "", 0, 0);
    dict_erlang->nodes = argv_split(nodes, " ,\t\r\n");
    myfree(nodes);

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
    if (dict_erlang->nodes)
        argv_free(dict_erlang->nodes);
    myfree(dict_erlang->cookie);
    myfree(dict_erlang->mod);
    myfree(dict_erlang->fun);
    if (dict_erlang->ctx)
        db_common_free_ctx(dict_erlang->ctx);
    if (dict->fold_buf)
        vstring_free(dict->fold_buf);
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
    if (dict_erlang->nodes->argc == 0)
        msg_fatal("no erlang nodes specified");
    dict_erlang->active_node = 0;

    if (dict_erlang->cookie == NULL)
        msg_fatal("no erlang cookie specified");
    if (dict_erlang->mod == NULL)
        msg_fatal("no erlang module specified");
    if (dict_erlang->fun == NULL)
        msg_fatal("no erlang function specified");

    return (DICT_DEBUG(&dict_erlang->dict));
}

#endif
