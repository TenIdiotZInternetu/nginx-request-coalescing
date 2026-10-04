#include "Cache.hpp"

extern "C" {
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

extern ngx_module_t ngx_request_coalescing_module;
}

namespace ngx::http::coalesce {

typedef struct {
    size_t ring_buffer_size;
    size_t ring_buffer_count;
    size_t max_entry_payload_size;
    ngx_shm_zone_t* cache_shm;
} server_config_t;

typedef struct {
    ngx_uint_t http_status;
    uint32_t headers_size;
    uint32_t body_size;
} cached_res_t;

static ngx_http_output_header_filter_pt next_header_filter;
static ngx_http_output_body_filter_pt next_body_filter;

static server_config_t default_config{16, 1024, 16384, NULL};

static ngx_int_t header_filter(ngx_http_request_t* r);
static ngx_int_t body_filter(ngx_http_request_t* r, ngx_chain_t* in);
static ngx_int_t request_handler(ngx_http_request_t* r);

static ngx_int_t postconfiguration(ngx_conf_t* cf);
static void* create_server_config(ngx_conf_t* cf);
static char* merge_server_config(ngx_conf_t* conf_ctx, void* parent, void* child);


static ngx_command_t module_commands[] = {//
    {ngx_string("cache_buffer_size"),
        NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(server_config_t, ring_buffer_size),
        NULL},

    {ngx_string("cache_buffer_count"),
        NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(server_config_t, ring_buffer_count),
        NULL},

    {ngx_string("cache_max_entry_size"),
        NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_SRV_CONF_OFFSET,
        offsetof(server_config_t, max_entry_payload_size),
        NULL},

    ngx_null_command};


static ngx_http_module_t module_ctx = {
    NULL,                 /* preconfiguration */
    postconfiguration,    /* postconfiguration */
    NULL,                 /* create main configuration */
    NULL,                 /* init main configuration */
    create_server_config, /* create server configuration */
    merge_server_config,  /* merge server configuration */
    NULL,                 /* create location configuration */
    NULL                  /* merge location configuration */
};


static ngx_int_t postconfiguration(ngx_conf_t* cf)
{
    ngx_http_core_main_conf_t* core_main_conf = (ngx_http_core_main_conf_t*)
        ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    ngx_http_handler_pt* handler = (ngx_http_handler_pt*)ngx_array_push(
        &core_main_conf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);

    if (handler == NULL) {
        return NGX_ERROR;
    }
    *handler = request_handler;

    next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = header_filter;

    next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = body_filter;

    return NGX_OK;
}


static void* create_server_config(ngx_conf_t* conf_ctx)
{
    server_config_t* conf_ptr = (server_config_t*)ngx_pcalloc(
        conf_ctx->pool, sizeof(server_config_t));

    if (conf_ptr == NULL) {
        return NULL;
    }

    conf_ptr->ring_buffer_size = NGX_CONF_UNSET_SIZE;
    conf_ptr->ring_buffer_count = NGX_CONF_UNSET_SIZE;
    conf_ptr->max_entry_payload_size = NGX_CONF_UNSET_SIZE;

    return conf_ptr;
}


static char* merge_server_config(ngx_conf_t* conf_ctx, void* parent, void* child)
{
    server_config_t* parent_conf = (server_config_t*)parent;
    server_config_t* child_conf = (server_config_t*)child;

    ngx_conf_merge_size_value(child_conf->ring_buffer_size,
        parent_conf->ring_buffer_size,
        default_config.ring_buffer_size);

    ngx_conf_merge_size_value(child_conf->ring_buffer_count,
        parent_conf->ring_buffer_count,
        default_config.ring_buffer_count);

    ngx_conf_merge_size_value(child_conf->max_entry_payload_size,
        parent_conf->max_entry_payload_size,
        default_config.max_entry_payload_size);

    // Shared memory
    child_conf->cache_shm = Cache::init(conf_ctx,
        child_conf->ring_buffer_count,
        child_conf->ring_buffer_size,
        child_conf->max_entry_payload_size,
        &ngx_request_coalescing_module);

    if (child_conf->cache_shm == NULL) {
        return (char*)NGX_CONF_ERROR;
    }

    return NGX_OK;
}


static ngx_uint_t get_cache_key(ngx_http_request_t* request, ngx_str_t* key)
{
    ngx_str_t host = request->headers_in.server;
    ngx_str_t uri = request->uri;
    ngx_uint_t key_length = host.len + uri.len;

    if (key_length == 0) {
        return NGX_ERROR;
    }

    key->data = (u_char*)ngx_pnalloc(request->pool, key_length);
    if (key->data == NULL) {
        return NGX_ERROR;
    }

    key->len = key_length;

    u_char* ptr = key->data;
    ngx_cpymem(ptr, request->headers_in.server.data, host.len);
    ngx_cpymem(ptr, request->uri.data, uri.len);
    return NGX_OK;
}


static ngx_int_t request_handler(ngx_http_request_t* request)
{
    ngx_chain_t output_chain;

    server_config_t* srv_config = (server_config_t*)ngx_http_get_module_srv_conf(
        request, ngx_request_coalescing_module);

    Cache* cache = (Cache*)srv_config->cache_shm->data;
    ngx_str_t cache_key;

    if (get_cache_key(request, &cache_key) != NGX_OK) {
        return NGX_ERROR;
    };

    if (cache->key_exists(cache_key)) {
        ngx_event_t* event = (ngx_event_t*)ngx_pcalloc(request->pool, sizeof(ngx_event_t));
        event->handler = check_cached_payload;
        ngx_event_add_timer(event, 10);

        ++request->main->count;
        return NGX_DONE;
    }

    cache->add_entry(cache_key);
    return NGX_DECLINED;
}


static ngx_int_t header_filter(ngx_http_request_t* r)
{
    // TODO: get the request ctx; if there is none (not a leader), pass through.
    // Leader: if content_length_n is -1 (chunked) or too big for the slot,
    // set the entry to REFUSED. Otherwise store the status and headers
    // in the payload, after cached_res_t.
    return next_header_filter(r);
}


static ngx_int_t body_filter(ngx_http_request_t* r, ngx_chain_t* in)
{
    // TODO: no ctx -> pass through.
    // Leader: copy each buffer into the slot, and on last_buf set COMPLETE.
    // If the body outgrows the slot mid-stream, set REFUSED and keep streaming.
    return next_body_filter(r, in);
}

}  // namespace ngx::http::coalesce


extern "C" {
ngx_module_t ngx_request_coalescing_module = {//
    NGX_MODULE_V1,
    &ngx::http::coalesce::module_ctx,     /* module context */
    ngx::http::coalesce::module_commands, /* module directives */
    NGX_HTTP_MODULE,                      /* module type */
    NULL,                                 /* init master */
    NULL,                                 /* init module */
    NULL,                                 /* init process */
    NULL,                                 /* init thread */
    NULL,                                 /* exit thread */
    NULL,                                 /* exit process */
    NULL,                                 /* exit master */
    NGX_MODULE_V1_PADDING};
}  // extern "C"
