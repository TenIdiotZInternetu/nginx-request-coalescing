extern "C" {
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace ngx::http::coalesce {

typedef struct {
    size_t ring_buffer_size;
    size_t ring_buffer_count;
} server_config_t;

static server_config_t default_config{16, 4096};

static ngx_int_t request_handler(ngx_http_request_t* r);
static void* create_server_config(ngx_conf_t* cf);
static char* merge_server_config(ngx_conf_t* conf_ctx, void* parent, void* child);


/* Directives list */
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

    ngx_null_command};

/* Module context callbacks */
static ngx_http_module_t module_ctx = {
    NULL,                 /* preconfiguration */
    NULL,                 /* postconfiguration */
    NULL,                 /* create main configuration */
    NULL,                 /* init main configuration */
    create_server_config, /* create server configuration */
    merge_server_config,  /* merge server configuration */
    NULL,                 /* create location configuration */
    NULL                  /* merge location configuration */
};


static void* create_server_config(ngx_conf_t* conf_ctx)
{
    server_config_t* conf_ptr = (server_config_t*)ngx_pcalloc(
        conf_ctx->pool, sizeof(server_config_t));

    if (conf_ptr == NULL) {
        return NULL;
    }

    conf_ptr->ring_buffer_size = NGX_CONF_UNSET_SIZE;
    conf_ptr->ring_buffer_count = NGX_CONF_UNSET_SIZE;

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

    // TODO: init shared memory

    // Assign request handler
    ngx_http_core_loc_conf_t* core_loc_conf = (ngx_http_core_loc_conf_t*)
        ngx_http_conf_get_module_loc_conf(conf_ctx, ngx_http_core_module);

    core_loc_conf->handler = request_handler;
    return NGX_CONF_OK;
}


static ngx_int_t request_handler(ngx_http_request_t* request)
{
    ngx_chain_t output_chain;

    // TODO: Look for the key in cache
    //              if present, wait for event, fetch result from shmem
    //              if not, pass request

    return ngx_http_output_filter(request, &output_chain);
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
