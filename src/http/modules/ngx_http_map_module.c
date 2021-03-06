
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_uint_t                  hash_max_size;
    ngx_uint_t                  hash_bucket_size;
} ngx_http_map_conf_t;


typedef struct {
    ngx_hash_keys_arrays_t      keys;

    ngx_array_t                *values_hash;

    ngx_http_variable_value_t  *default_value;
    ngx_uint_t                  hostnames;      /* unsigned  hostnames:1 */
} ngx_http_map_conf_ctx_t;


typedef struct {
    ngx_hash_t                  hash;
    ngx_hash_wildcard_t        *dns_wildcards;
    ngx_int_t                   index;
    ngx_http_variable_value_t  *default_value;
    ngx_uint_t                  hostnames;      /* unsigned  hostnames:1 */
} ngx_http_map_ctx_t;


static int ngx_libc_cdecl ngx_http_map_cmp_dns_wildcards(const void *one,
    const void *two);
static void *ngx_http_map_create_conf(ngx_conf_t *cf);
static char *ngx_http_map_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_map(ngx_conf_t *cf, ngx_command_t *dummy, void *conf);


static ngx_command_t  ngx_http_map_commands[] = {

    { ngx_string("map"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE2,
      ngx_http_map_block,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("map_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_map_conf_t, hash_max_size),
      NULL },

    { ngx_string("map_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_map_conf_t, hash_bucket_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_map_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_map_create_conf,              /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_map_module = {
    NGX_MODULE_V1,
    &ngx_http_map_module_ctx,              /* module context */
    ngx_http_map_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_map_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_http_map_ctx_t  *map = (ngx_http_map_ctx_t *) data;

    size_t                      len;
    u_char                     *name;
    ngx_uint_t                  key, i;
    ngx_http_variable_value_t  *vv, *value;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http map started");

    vv = ngx_http_get_flushed_variable(r, map->index);

    if (vv == NULL || vv->not_found) {
        *v = *map->default_value;
        return NGX_OK;
    }

    len = vv->len;

    if (len && map->hostnames && vv->data[len - 1] == '.') {
        len--;
    }

    if (len == 0) {
        *v = *map->default_value;
        return NGX_OK;
    }

    name = ngx_palloc(r->pool, len);
    if (name == NULL) {
        return NGX_ERROR;
    }

    key = 0;
    for (i = 0; i < len; i++) {
        name[i] = ngx_tolower(vv->data[i]);
        key = ngx_hash(key, name[i]);
    }

    value = NULL;

    if (map->hash.buckets) {
        value = ngx_hash_find(&map->hash, key, name, len);
    }

    if (value) {
        *v = *value;

    } else {
        if (map->dns_wildcards && map->dns_wildcards->hash.buckets) {
            value = ngx_hash_find_wildcard(map->dns_wildcards, name, len);
            if (value) {
                *v = *value;

            } else {
                *v = *map->default_value;
            }

        } else {
            *v = *map->default_value;
        }
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http map: \"%V\" \"%V\"", vv, v);

    return NGX_OK;
}


static void *
ngx_http_map_create_conf(ngx_conf_t *cf)
{
    ngx_http_map_conf_t  *mcf;

    mcf = ngx_palloc(cf->pool, sizeof(ngx_http_map_conf_t));
    if (mcf == NULL) {
        return NGX_CONF_ERROR;
    }

    mcf->hash_max_size = NGX_CONF_UNSET_UINT;
    mcf->hash_bucket_size = NGX_CONF_UNSET_UINT;

    return mcf;
}


static char *
ngx_http_map_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_map_conf_t  *mcf = conf;

    char                      *rv;
    ngx_str_t                 *value, name;
    ngx_conf_t                 save;
    ngx_pool_t                *pool;
    ngx_hash_init_t            hash;
    ngx_http_map_ctx_t        *map;
    ngx_http_variable_t       *var;
    ngx_http_map_conf_ctx_t    ctx;

    if (mcf->hash_max_size == NGX_CONF_UNSET_UINT) {
        mcf->hash_max_size = 2048;
    }

    if (mcf->hash_bucket_size == NGX_CONF_UNSET_UINT) {
        mcf->hash_bucket_size = ngx_cacheline_size;

    } else {
        mcf->hash_bucket_size = ngx_align(mcf->hash_bucket_size,
                                          ngx_cacheline_size);
    }

    map = ngx_pcalloc(cf->pool, sizeof(ngx_http_map_ctx_t));
    if (map == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    name = value[1];
    name.len--;
    name.data++;

    map->index = ngx_http_get_variable_index(cf, &name);

    if (map->index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    name = value[2];
    name.len--;
    name.data++;

    var = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_CHANGABLE);
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    var->get_handler = ngx_http_map_variable;
    var->data = (uintptr_t) map;

    pool = ngx_create_pool(16384, cf->log);
    if (pool == NULL) {
        return NGX_CONF_ERROR;
    }

    ctx.keys.pool = cf->pool;
    ctx.keys.temp_pool = pool;

    if (ngx_hash_keys_array_init(&ctx.keys, NGX_HASH_LARGE) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_CONF_ERROR;
    }

    ctx.values_hash = ngx_pcalloc(pool, sizeof(ngx_array_t) * ctx.keys.hsize);
    if (ctx.values_hash == NULL) {
        ngx_destroy_pool(pool);
        return NGX_CONF_ERROR;
    }

    ctx.default_value = NULL;
    ctx.hostnames = 0;

    save = *cf;
    cf->pool = pool;
    cf->ctx = &ctx;
    cf->handler = ngx_http_map;
    cf->handler_conf = conf;

    rv = ngx_conf_parse(cf, NULL);

    *cf = save;

    if (rv != NGX_CONF_OK) {
        ngx_destroy_pool(pool);
        return rv;
    }

    hash.key = ngx_hash_key_lc;
    hash.max_size = mcf->hash_max_size;
    hash.bucket_size = mcf->hash_bucket_size;
    hash.name = "map_hash";
    hash.pool = cf->pool;

    if (ctx.keys.keys.nelts) {
        hash.hash = &map->hash;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, ctx.keys.keys.elts, ctx.keys.keys.nelts)
            != NGX_OK)
        {
            ngx_destroy_pool(pool);
            return NGX_CONF_ERROR;
        }
    }

    map->default_value = ctx.default_value ? ctx.default_value:
                                             &ngx_http_variable_null_value;

    if (ctx.keys.dns_wildcards.nelts) {

        ngx_qsort(ctx.keys.dns_wildcards.elts,
                  (size_t) ctx.keys.dns_wildcards.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_map_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = pool;

        if (ngx_hash_wildcard_init(&hash, ctx.keys.dns_wildcards.elts,
                                   ctx.keys.dns_wildcards.nelts)
            != NGX_OK)
        {
            ngx_destroy_pool(pool);
            return NGX_CONF_ERROR;
        }

        map->dns_wildcards = (ngx_hash_wildcard_t *) hash.hash;
    }

    ngx_destroy_pool(pool);

    return rv;
}


static int ngx_libc_cdecl
ngx_http_map_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_strcmp(first->key.data, second->key.data);
}


static char *
ngx_http_map(ngx_conf_t *cf, ngx_command_t *dummy, void *conf)
{
    u_char                      ch;
    ngx_int_t                   rc;
    ngx_str_t                  *value, file;
    ngx_uint_t                  i, key, flags;
    ngx_http_map_conf_ctx_t    *ctx;
    ngx_http_variable_value_t  *var, **vp;

    ctx = cf->ctx;

    value = cf->args->elts;

    if (cf->args->nelts == 1
        && ngx_strcmp(value[0].data, "hostnames") == 0)
    {
        ctx->hostnames = 1;
        return NGX_CONF_OK;

    } else if (cf->args->nelts != 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number of the map parameters");
        return NGX_CONF_ERROR;

    } else if (value[0].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid first parameter");
        return NGX_CONF_ERROR;
    }

    if (ngx_strcmp(value[0].data, "include") == 0) {
        file = value[1];

        if (ngx_conf_full_name(cf->cycle, &file) == NGX_ERROR){
            return NGX_CONF_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        return ngx_conf_parse(cf, &file);
    }

    key = 0;

    for (i = 0; i < value[1].len; i++) {
        key = ngx_hash(key, value[1].data[i]);
    }

    key %= ctx->keys.hsize;

    vp = ctx->values_hash[key].elts;

    if (vp) {
        for (i = 0; i < ctx->values_hash[key].nelts; i++) {
            if (value[1].len != (size_t) vp[i]->len) {
                continue;
            }

            if (ngx_strncmp(value[1].data, vp[i]->data, value[1].len) == 0) {
                var = vp[i];
                goto found;
            }
        }

    } else {
        if (ngx_array_init(&ctx->values_hash[key], cf->pool, 4,
                           sizeof(ngx_http_variable_value_t *))
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    var = ngx_palloc(ctx->keys.pool, sizeof(ngx_http_variable_value_t));
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    var->len = value[1].len;
    var->data = ngx_pstrdup(ctx->keys.pool, &value[1]);
    if (var->data == NULL) {
        return NGX_CONF_ERROR;
    }

    var->valid = 1;
    var->no_cachable = 0;
    var->not_found = 0;

    vp = ngx_array_push(&ctx->values_hash[key]);
    if (vp == NULL) {
        return NGX_CONF_ERROR;
    }

    *vp = var;

found:

    ch = value[0].data[0];

    if ((ch != '*' && ch != '.') || ctx->hostnames == 0) {

        if (ngx_strcmp(value[0].data, "default") == 0) {

            if (ctx->default_value) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "duplicate default map parameter");
                return NGX_CONF_ERROR;
            }

            ctx->default_value = var;

            return NGX_CONF_OK;
        }

        if (value[0].len && ch == '!') {
            value[0].len--;
            value[0].data++;
        }

        flags = 0;

    } else {

        if ((ch == '*' && (value[0].len < 3 || value[0].data[1] != '.'))
            || (ch == '.' && value[0].len < 2))
        {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid DNS wildcard \"%V\"", &value[0]);

            return NGX_CONF_ERROR;
        }

        flags = NGX_HASH_WILDCARD_KEY;
    }

    rc = ngx_hash_add_key(&ctx->keys, &value[0], var, flags);

    if (rc == NGX_OK) {
        return NGX_CONF_OK;
    }

    if (rc == NGX_BUSY) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "conflicting parameter \"%V\"", &value[0]);
    }

    return NGX_CONF_ERROR;
}
