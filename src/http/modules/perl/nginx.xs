
/*
 * Copyright (C) Igor Sysoev
 */


#define PERL_NO_GET_CONTEXT

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_perl_module.h>

#include "XSUB.h"

#define ngx_http_perl_set_request(r)                                          \
    r = INT2PTR(ngx_http_request_t *, SvIV((SV *) SvRV(ST(0))))


#define ngx_http_perl_set_targ(p, len, z)                                     \
                                                                              \
    sv_upgrade(TARG, SVt_PV);                                                 \
    SvPOK_on(TARG);                                                           \
    SvPV_set(TARG, (char *) p);                                               \
    SvLEN_set(TARG, len + z);                                                 \
    SvCUR_set(TARG, len);                                                     \
    SvFAKE_on(TARG);                                                          \
    SvREADONLY_on(TARG);                                                      \


static ngx_int_t
ngx_http_perl_sv2str(pTHX_ ngx_http_request_t *r, ngx_str_t *s, SV *sv)
{
    u_char  *p;
    STRLEN   len;

    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PV) {
        sv = SvRV(sv);
    }

    p = (u_char *) SvPV(sv, len);

    s->len = len;

    if (SvREADONLY(sv)) {
        s->data = p;
        return NGX_OK;
    }

    s->data = ngx_palloc(r->pool, len);
    if (s->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s->data, p, len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_perl_output(ngx_http_request_t *r, ngx_buf_t *b)
{
    ngx_chain_t           out;
#if (NGX_HTTP_SSI)
    ngx_chain_t          *cl;
    ngx_http_perl_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_perl_module);

    if (ctx->ssi) {
        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = b;
        cl->next = NULL;
        *ctx->ssi->last_out = cl;
        ctx->ssi->last_out = &cl->next;

        return NGX_OK;
    }
#endif

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


MODULE = nginx    PACKAGE = nginx


void
send_http_header(r, ...)
    CODE:

    ngx_http_request_t  *r;
    SV                  *sv;

    ngx_http_perl_set_request(r);

    if (r->headers_out.status == 0) {
        r->headers_out.status = NGX_HTTP_OK;
    }

    if (items != 1) {
        sv = ST(1);

        if (ngx_http_perl_sv2str(aTHX_ r, &r->headers_out.content_type, sv)
            != NGX_OK)
        {
            XSRETURN_EMPTY;
        }

    } else {
        if (ngx_http_set_content_type(r) != NGX_OK) {
            XSRETURN_EMPTY;
        }
    }

    (void) ngx_http_send_header(r);


void
header_only(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);

    sv_upgrade(TARG, SVt_IV);
    sv_setiv(TARG, r->header_only);

    ST(0) = TARG;


void
uri(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);
    ngx_http_perl_set_targ(r->uri.data, r->uri.len, 0);

    ST(0) = TARG;


void
args(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);
    ngx_http_perl_set_targ(r->args.data, r->args.len, 0);

    ST(0) = TARG;


void
request_method(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);
    ngx_http_perl_set_targ(r->method_name.data, r->method_name.len, 0);

    ST(0) = TARG;


void
remote_addr(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);
    ngx_http_perl_set_targ(r->connection->addr_text.data,
                           r->connection->addr_text.len, 1);

    ST(0) = TARG;


void
header_in(r, key)
    CODE:

    dXSTARG;
    ngx_http_request_t         *r;
    SV                         *key;
    u_char                     *p, *lowcase_key, *cookie;
    STRLEN                      len;
    ssize_t                     size;
    ngx_uint_t                  i, n, hash;
    ngx_list_part_t            *part;
    ngx_table_elt_t            *h, **ph;
    ngx_http_header_t          *hh;
    ngx_http_core_main_conf_t  *cmcf;

    ngx_http_perl_set_request(r);

    key = ST(1);

    if (SvROK(key) && SvTYPE(SvRV(key)) == SVt_PV) {
        key = SvRV(key);
    }

    p = (u_char *) SvPV(key, len);

    /* look up hashed headers */

    lowcase_key = ngx_palloc(r->pool, len);
    if (lowcase_key == NULL) {
        XSRETURN_UNDEF;
    }

    hash = 0;
    for (i = 0; i < len; i++) {
        lowcase_key[i] = ngx_tolower(p[i]);
        hash = ngx_hash(hash, lowcase_key[i]);
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    hh = ngx_hash_find(&cmcf->headers_in_hash, hash, lowcase_key, len);

    if (hh) {
        if (hh->offset) {

            ph = (ngx_table_elt_t **) ((char *) &r->headers_in + hh->offset);

            if (*ph) {
                ngx_http_perl_set_targ((*ph)->value.data, (*ph)->value.len, 0);

                goto done;
            }

            XSRETURN_UNDEF;
        }

        /* Cookie */

        n = r->headers_in.cookies.nelts;

        if (n == 0) {
            XSRETURN_UNDEF;
        }

        ph = r->headers_in.cookies.elts;

        if (n == 1) {
            ngx_http_perl_set_targ((*ph)->value.data, (*ph)->value.len, 0);

            goto done;
        }

        size = - (ssize_t) (sizeof("; ") - 1);

        for (i = 0; i < n; i++) {
            size += ph[i]->value.len + sizeof("; ") - 1;
        }

        cookie = ngx_palloc(r->pool, size);
        if (cookie == NULL) {
            XSRETURN_UNDEF;
        }

        p = cookie;

        for (i = 0; /* void */ ; i++) {
            p = ngx_copy(p, ph[i]->value.data, ph[i]->value.len);

            if (i == n - 1) {
                break;
            }

            *p++ = ';'; *p++ = ' ';
        }

        ngx_http_perl_set_targ(cookie, size, 0);

        goto done;
    }

    /* iterate over all headers */

    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (len != h[i].key.len
            || ngx_strcasecmp(p, h[i].key.data) != 0)
        {
            continue;
        }

        ngx_http_perl_set_targ(h[i].value.data, h[i].value.len, 0);

        goto done;
    }

    XSRETURN_UNDEF;

    done:

    ST(0) = TARG;


void
has_request_body(r, next)
    CODE:

    dXSTARG;
    ngx_http_request_t   *r;
    SV                   *next;
    ngx_http_perl_ctx_t  *ctx;

    ngx_http_perl_set_request(r);

    if (r->headers_in.content_length_n <= 0) {
        XSRETURN_UNDEF;
    }

    next = ST(1);

    ctx = ngx_http_get_module_ctx(r, ngx_http_perl_module);
    ctx->next = next;

    r->request_body_in_single_buf = 1;
    r->request_body_in_persistent_file = 1;
    r->request_body_delete_incomplete_file = 1;

    if (r->request_body_in_file_only) {
        r->request_body_file_log_level = 0;
    }

    ngx_http_read_client_request_body(r, ngx_http_perl_handle_request);

    sv_upgrade(TARG, SVt_IV);
    sv_setiv(TARG, 1);

    ST(0) = TARG;


void
request_body(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;
    size_t               len;

    ngx_http_perl_set_request(r);

    if (r->request_body->temp_file || r->request_body->bufs == NULL) {
        XSRETURN_UNDEF;
    }

    len = r->request_body->bufs->buf->last - r->request_body->bufs->buf->pos;

    if (len == 0) {
        XSRETURN_UNDEF;
    }

    ngx_http_perl_set_targ(r->request_body->bufs->buf->pos, len, 0);

    ST(0) = TARG;


void
request_body_file(r)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);

    if (r->request_body->temp_file == NULL) {
        XSRETURN_UNDEF;
    }

    ngx_http_perl_set_targ(r->request_body->temp_file->file.name.data,
                           r->request_body->temp_file->file.name.len, 1);

    ST(0) = TARG;


void
header_out(r, key, value)
    CODE:

    ngx_http_request_t  *r;
    SV                  *key;
    SV                  *value;
    ngx_table_elt_t     *header;

    ngx_http_perl_set_request(r);

    key = ST(1);
    value = ST(2);

    header = ngx_list_push(&r->headers_out.headers);
    if (header == NULL) {
        XSRETURN_EMPTY;
    }

    header->hash = 1;

    if (ngx_http_perl_sv2str(aTHX_ r, &header->key, key) != NGX_OK) {
        XSRETURN_EMPTY;
    }

    if (ngx_http_perl_sv2str(aTHX_ r, &header->value, value) != NGX_OK) {
        XSRETURN_EMPTY;
    }

    if (header->key.len == sizeof("Content-Length") - 1
        && ngx_strncasecmp(header->key.data, "Content-Length",
                           sizeof("Content-Length") - 1) == 0)
    {
        r->headers_out.content_length_n = (off_t) SvIV(value);
        r->headers_out.content_length = header;
    }

    XSRETURN_EMPTY;


void
filename(r)
    CODE:

    dXSTARG;
    size_t                root;
    ngx_http_request_t   *r;
    ngx_http_perl_ctx_t  *ctx;

    ngx_http_perl_set_request(r);

    ctx = ngx_http_get_module_ctx(r, ngx_http_perl_module);
    if (ctx->filename.data) {
        goto done;
    }

    if (ngx_http_map_uri_to_path(r, &ctx->filename, &root, 0) == NULL) {
        XSRETURN_UNDEF;
    }

    ctx->filename.len--;
    sv_setpv(PL_statname, (char *) ctx->filename.data);

    done:

    ngx_http_perl_set_targ(ctx->filename.data, ctx->filename.len, 1);

    ST(0) = TARG;


void
print(r, ...)
    CODE:

    ngx_http_request_t  *r;
    SV                  *sv;
    int                  i;
    u_char              *p;
    size_t               size;
    STRLEN               len;
    ngx_buf_t           *b;

    ngx_http_perl_set_request(r);

    if (items == 2) {

        /*
         * do zero copy for prolate single read-only SV:
         *     $r->print("some text\n");
         */

        sv = ST(1);

        if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PV) {
            sv = SvRV(sv);
        }

        if (SvREADONLY(sv)) {

            p = (u_char *) SvPV(sv, len);

            if (len == 0) {
                XSRETURN_EMPTY;
            }

            b = ngx_calloc_buf(r->pool);
            if (b == NULL) {
                XSRETURN_EMPTY;
            }

            b->memory = 1;
            b->pos = p;
            b->last = p + len;
            b->start = p;
            b->end = b->last;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "$r->print: read-only SV: %z", len);

            goto out;
        }
    }

    size = 0;

    for (i = 1; i < items; i++) {

        sv = ST(i);

        if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PV) {
            sv = SvRV(sv);
        }

        (void) SvPV(sv, len);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "$r->print: copy SV: %z", len);

        size += len;
    }

    if (size == 0) {
        XSRETURN_EMPTY;
    }

    b = ngx_create_temp_buf(r->pool, size);
    if (b == NULL) {
        XSRETURN_EMPTY;
    }

    for (i = 1; i < items; i++) {
        sv = ST(i);

        if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PV) {
            sv = SvRV(sv);
        }

        p = (u_char *) SvPV(sv, len);
        b->last = ngx_cpymem(b->last, p, len);
    }

    out:

    (void) ngx_http_perl_output(r, b);

    XSRETURN_EMPTY;


void
sendfile(r, filename, offset = -1, bytes = 0)
    CODE:

    ngx_http_request_t       *r;
    char                     *filename;
    int                       offset;
    size_t                    bytes;
    ngx_fd_t                  fd;
    ngx_buf_t                *b;
    ngx_file_info_t           fi;
    ngx_pool_cleanup_t       *cln;
    ngx_pool_cleanup_file_t  *clnf;

    ngx_http_perl_set_request(r);

    filename = SvPV_nolen(ST(1));

    if (filename == NULL) {
        croak("sendfile(): NULL filename");
    }

    offset = items < 3 ? -1 : SvIV(ST(2));
    bytes = items < 4 ? 0 : SvIV(ST(3));

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        XSRETURN_EMPTY;
    }

    b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
    if (b->file == NULL) {
        XSRETURN_EMPTY;
    }

    cln = ngx_pool_cleanup_add(r->pool, sizeof(ngx_pool_cleanup_file_t));
    if (cln == NULL) {
        XSRETURN_EMPTY;
    }

    fd = ngx_open_file((u_char *) filename, NGX_FILE_RDONLY, NGX_FILE_OPEN);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", filename);
        XSRETURN_EMPTY;
    }

    if (offset == -1) {
        offset = 0;
    }

    if (bytes == 0) {
        if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                          ngx_fd_info_n " \"%s\" failed", filename);

            if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, ngx_errno,
                              ngx_close_file_n " \"%s\" failed", filename);
            }

            XSRETURN_EMPTY;
        }

        bytes = ngx_file_size(&fi) - offset;
    }

    cln->handler = ngx_pool_cleanup_file;
    clnf = cln->data;

    clnf->fd = fd;
    clnf->name = (u_char *) "";
    clnf->log = r->pool->log;

    b->in_file = 1;

    b->file_pos = offset;
    b->file_last = offset + bytes;

    b->file->fd = fd;
    b->file->log = r->connection->log;

    (void) ngx_http_perl_output(r, b);

    XSRETURN_EMPTY;


void
rflush(r)
    CODE:

    ngx_http_request_t  *r;
    ngx_buf_t           *b;

    ngx_http_perl_set_request(r);

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        XSRETURN_EMPTY;
    }

    b->flush = 1;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "$r->rflush");

    (void) ngx_http_perl_output(r, b);

    XSRETURN_EMPTY;


void
internal_redirect(r, uri)
    CODE:

    ngx_http_request_t   *r;
    SV                   *uri;
    ngx_uint_t            i;
    ngx_http_perl_ctx_t  *ctx;

    ngx_http_perl_set_request(r);

    uri = ST(1);

    ctx = ngx_http_get_module_ctx(r, ngx_http_perl_module);

    if (ngx_http_perl_sv2str(aTHX_ r, &ctx->redirect_uri, uri) != NGX_OK) {
        XSRETURN_EMPTY;
    }

    for (i = 0; i < ctx->redirect_uri.len; i++) {
        if (ctx->redirect_uri.data[i] == '?') {

            ctx->redirect_args.len = ctx->redirect_uri.len - (i + 1);
            ctx->redirect_args.data = &ctx->redirect_uri.data[i + 1];
            ctx->redirect_uri.len = i;

            XSRETURN_EMPTY;
        }
    }


void
allow_ranges(r)
    CODE:

    ngx_http_request_t  *r;

    ngx_http_perl_set_request(r);

    r->allow_ranges = 1;

    XSRETURN_EMPTY;


void
unescape(r, text, type = 0)
    CODE:

    dXSTARG;
    ngx_http_request_t  *r;
    SV                  *text;
    int                  type;
    u_char              *p, *dst, *src;
    STRLEN               len;

    ngx_http_perl_set_request(r);

    text = ST(1);

    src = (u_char *) SvPV(text, len);

    p = ngx_palloc(r->pool, len + 1);
    if (p == NULL) {
        XSRETURN_UNDEF;
    }

    dst = p;

    type = items < 3 ? 0 : SvIV(ST(2));

    ngx_unescape_uri(&dst, &src, len, (ngx_uint_t) type);
    *dst = '\0';

    ngx_http_perl_set_targ(p, dst - p, 1);

    ST(0) = TARG;


void
variable(r, name, value = NULL)
    CODE:

    dXSTARG;
    ngx_http_request_t         *r;
    SV                         *name, *value;
    u_char                     *p, *lowcase;
    STRLEN                      len;
    ngx_str_t                   var, val;
    ngx_uint_t                  i, hash;
    ngx_http_variable_value_t  *vv;

    ngx_http_perl_set_request(r);

    name = ST(1);

    if (SvROK(name) && SvTYPE(SvRV(name)) == SVt_PV) {
        name = SvRV(name);
    }

    if (items == 2) {
        value = NULL;

    } else {
        value = ST(2);

        if (SvROK(value) && SvTYPE(SvRV(value)) == SVt_PV) {
            value = SvRV(value);
        }

        if (ngx_http_perl_sv2str(aTHX_ r, &val, value) != NGX_OK) {
            XSRETURN_UNDEF;
        }
    }

    p = (u_char *) SvPV(name, len);

    lowcase = ngx_palloc(r->pool, len);
    if (lowcase == NULL) {
        XSRETURN_UNDEF;
    }

    hash = 0;
    for (i = 0; i < len; i++) {
        lowcase[i] = ngx_tolower(p[i]);
        hash = ngx_hash(hash, lowcase[i]);
    }

    var.len = len;
    var.data = lowcase;

    vv = ngx_http_get_variable(r, &var, hash, 1);
    if (vv == NULL) {
        XSRETURN_UNDEF;
    }

    if (vv->not_found) {
        if (value == NULL) {
            XSRETURN_UNDEF;
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "variable \"%V\" not found", &var);

        XSRETURN_UNDEF;
    }

    if (value) {
        vv->len = val.len;
        vv->valid = 1;
        vv->no_cachable = 0;
        vv->not_found = 0;
        vv->data = val.data;

        XSRETURN_UNDEF;
    }

    ngx_http_perl_set_targ(vv->data, vv->len, 0);

    ST(0) = TARG;
