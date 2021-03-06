
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_event_pipe.h>
#include <ngx_http.h>


#define NGX_HTTP_UPSTREAM_FT_ERROR           0x00000002
#define NGX_HTTP_UPSTREAM_FT_TIMEOUT         0x00000004
#define NGX_HTTP_UPSTREAM_FT_INVALID_HEADER  0x00000008
#define NGX_HTTP_UPSTREAM_FT_HTTP_500        0x00000010
#define NGX_HTTP_UPSTREAM_FT_HTTP_503        0x00000020
#define NGX_HTTP_UPSTREAM_FT_HTTP_404        0x00000040
#define NGX_HTTP_UPSTREAM_FT_BUSY_LOCK       0x00000080
#define NGX_HTTP_UPSTREAM_FT_MAX_WAITING     0x00000100
#define NGX_HTTP_UPSTREAM_FT_OFF             0x80000000


#define NGX_HTTP_UPSTREAM_INVALID_HEADER     40


typedef struct {
    ngx_msec_t                      bl_time;
    ngx_uint_t                      bl_state;

    ngx_uint_t                      status;
    ngx_msec_t                      response_time;

    ngx_str_t                      *peer;
} ngx_http_upstream_state_t;


typedef struct {
    ngx_hash_t                      headers_in_hash;
    ngx_array_t                     upstreams;
                                             /* ngx_http_upstream_srv_conf_t */
} ngx_http_upstream_main_conf_t;

typedef struct ngx_http_upstream_srv_conf_s  ngx_http_upstream_srv_conf_t;

typedef ngx_int_t (*ngx_http_upstream_init_pt)(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
typedef ngx_int_t (*ngx_http_upstream_init_peer_pt)(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);


typedef struct {
    ngx_http_upstream_init_pt       init_upstream;
    ngx_http_upstream_init_peer_pt  init;
    void                           *data;
} ngx_http_upstream_peer_t;


typedef struct {
    ngx_peer_addr_t                *addrs;
    ngx_uint_t                      naddrs;
    ngx_uint_t                      weight;
    ngx_uint_t                      max_fails;
    time_t                          fail_timeout;

    unsigned                        down:1;
    unsigned                        backup:1;
} ngx_http_upstream_server_t;


#define NGX_HTTP_UPSTREAM_CREATE        0x0001
#define NGX_HTTP_UPSTREAM_WEIGHT        0x0002
#define NGX_HTTP_UPSTREAM_MAX_FAILS     0x0004
#define NGX_HTTP_UPSTREAM_FAIL_TIMEOUT  0x0008
#define NGX_HTTP_UPSTREAM_DOWN          0x0010
#define NGX_HTTP_UPSTREAM_BACKUP        0x0020


struct ngx_http_upstream_srv_conf_s {
    ngx_http_upstream_peer_t        peer;
    void                          **srv_conf;

    ngx_array_t                    *servers;   /* ngx_http_upstream_server_t */

    ngx_uint_t                      flags;
    ngx_str_t                       host;
    ngx_str_t                       file_name;
    ngx_uint_t                      line;
    in_port_t                       port;
};


typedef struct {
    ngx_http_upstream_srv_conf_t   *upstream;

    ngx_msec_t                      connect_timeout;
    ngx_msec_t                      send_timeout;
    ngx_msec_t                      read_timeout;
    ngx_msec_t                      timeout;

    size_t                          send_lowat;
    size_t                          buffer_size;

    size_t                          busy_buffers_size;
    size_t                          max_temp_file_size;
    size_t                          temp_file_write_size;

    size_t                          busy_buffers_size_conf;
    size_t                          max_temp_file_size_conf;
    size_t                          temp_file_write_size_conf;

    ngx_uint_t                      next_upstream;

    ngx_bufs_t                      bufs;

    ngx_flag_t                      buffering;
    ngx_flag_t                      pass_request_headers;
    ngx_flag_t                      pass_request_body;

    ngx_flag_t                      ignore_client_abort;
    ngx_flag_t                      intercept_errors;
    ngx_flag_t                      cyclic_temp_file;

    ngx_path_t                     *temp_path;

    ngx_hash_t                      hide_headers_hash;
    ngx_array_t                    *hide_headers;
    ngx_array_t                    *pass_headers;

    ngx_str_t                       schema;
    ngx_str_t                       uri;
    ngx_str_t                       location;
    ngx_str_t                       url;  /* used in proxy_rewrite_location */

    unsigned                        intercept_404:1;
    unsigned                        change_buffering:1;

#if (NGX_HTTP_SSL)
    ngx_ssl_t                      *ssl;
#endif

} ngx_http_upstream_conf_t;


typedef struct {
    ngx_str_t                       name;
    ngx_http_header_handler_pt      handler;
    ngx_uint_t                      offset;
    ngx_http_header_handler_pt      copy_handler;
    ngx_uint_t                      conf;
    ngx_uint_t                      redirect;  /* unsigned   redirect:1; */
} ngx_http_upstream_header_t;


typedef struct {
    ngx_list_t                      headers;

    ngx_uint_t                      status_n;
    ngx_str_t                       status_line;

    ngx_table_elt_t                *status;
    ngx_table_elt_t                *date;
    ngx_table_elt_t                *server;
    ngx_table_elt_t                *connection;

    ngx_table_elt_t                *expires;
    ngx_table_elt_t                *etag;
    ngx_table_elt_t                *x_accel_expires;
    ngx_table_elt_t                *x_accel_redirect;
    ngx_table_elt_t                *x_accel_limit_rate;

    ngx_table_elt_t                *content_type;
    ngx_table_elt_t                *content_length;

    ngx_table_elt_t                *last_modified;
    ngx_table_elt_t                *location;
    ngx_table_elt_t                *accept_ranges;
    ngx_table_elt_t                *www_authenticate;

#if (NGX_HTTP_GZIP)
    ngx_table_elt_t                *content_encoding;
#endif

    ngx_array_t                     cache_control;
} ngx_http_upstream_headers_in_t;


struct ngx_http_upstream_s {
    ngx_peer_connection_t           peer;

    ngx_event_pipe_t               *pipe;

    ngx_chain_t                    *request_bufs;

    ngx_output_chain_ctx_t          output;
    ngx_chain_writer_ctx_t          writer;

    ngx_http_upstream_conf_t       *conf;

    ngx_http_upstream_headers_in_t  headers_in;

    ngx_buf_t                       buffer;
    size_t                          length;

    ngx_chain_t                    *out_bufs;
    ngx_chain_t                    *busy_bufs;
    ngx_chain_t                    *free_bufs;

    ngx_int_t                     (*input_filter_init)(void *data);
    ngx_int_t                     (*input_filter)(void *data, ssize_t bytes);
    void                           *input_filter_ctx;

    ngx_int_t                     (*create_request)(ngx_http_request_t *r);
    ngx_int_t                     (*reinit_request)(ngx_http_request_t *r);
    ngx_int_t                     (*process_header)(ngx_http_request_t *r);
    void                          (*abort_request)(ngx_http_request_t *r);
    void                          (*finalize_request)(ngx_http_request_t *r,
                                        ngx_int_t rc);
    ngx_int_t                     (*rewrite_redirect)(ngx_http_request_t *r,
                                        ngx_table_elt_t *h, size_t prefix);

    ngx_msec_t                      timeout;

    ngx_str_t                       method;

    ngx_http_upstream_state_t      *state;
    ngx_array_t                     states;  /* of ngx_http_upstream_state_t */

    ngx_str_t                       uri;

    ngx_http_cleanup_pt            *cleanup;

    unsigned                        cachable:1;
    unsigned                        accel:1;

    unsigned                        buffering:1;

    unsigned                        request_sent:1;
    unsigned                        header_sent:1;
};


void ngx_http_upstream_init(ngx_http_request_t *r);
ngx_http_upstream_srv_conf_t *ngx_http_upstream_add(ngx_conf_t *cf,
    ngx_url_t *u, ngx_uint_t flags);


#define ngx_http_conf_upstream_srv_conf(uscf, module)                         \
    uscf->srv_conf[module.ctx_index]


extern ngx_module_t  ngx_http_upstream_module;


#endif /* _NGX_HTTP_UPSTREAM_H_INCLUDED_ */
