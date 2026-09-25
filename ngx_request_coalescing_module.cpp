extern "C" {
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

/* Location configuration structure storing the "echo" argument */
typedef struct {
    ngx_str_t echo_msg;
} ngx_http_echo_loc_conf_t;

/* Forward declarations */
static ngx_int_t ngx_http_echo_handler(ngx_http_request_t* r);
static char* ngx_http_echo_cmd(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static void* ngx_http_echo_create_loc_conf(ngx_conf_t* cf);
static char* ngx_http_echo_merge_loc_conf(ngx_conf_t* cf, void* parent, void* child);

/* Directives list */
static ngx_command_t ngx_echo_commands[] = {//
    {ngx_string("echo"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_http_echo_cmd,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_echo_loc_conf_t, echo_msg),
        NULL},
    ngx_null_command};

/* Module context callbacks */
static ngx_http_module_t ngx_http_echo_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */
    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */
    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */
    ngx_http_echo_create_loc_conf, /* create location configuration */
    ngx_http_echo_merge_loc_conf   /* merge location configuration */
};

/* Module definition (wrapped in block to avoid GCC/Clang initialization
 * warning) */
extern "C" {

ngx_module_t ngx_request_coalescing_module = {//
    NGX_MODULE_V1,
    &ngx_http_echo_module_ctx, /* module context */
    ngx_echo_commands,         /* module directives */
    NGX_HTTP_MODULE,           /* module type */
    NULL,                      /* init master */
    NULL,                      /* init module */
    NULL,                      /* init process */
    NULL,                      /* init thread */
    NULL,                      /* exit thread */
    NULL,                      /* exit process */
    NULL,                      /* exit master */
    NGX_MODULE_V1_PADDING};

}  // extern "C"

static void* ngx_http_echo_create_loc_conf(ngx_conf_t* conf_ctx)
{
    ngx_http_echo_loc_conf_t* new_loc_conf = (ngx_http_echo_loc_conf_t*)ngx_pcalloc(
        conf_ctx->pool, sizeof(ngx_http_echo_loc_conf_t));

    if (new_loc_conf == NULL) {
        return NULL;
    }

    return new_loc_conf;
}

static char* ngx_http_echo_merge_loc_conf(ngx_conf_t* conf_ctx, void* parent, void* child)
{
    ngx_http_echo_loc_conf_t* parent_loc_conf = (ngx_http_echo_loc_conf_t*)parent;
    ngx_http_echo_loc_conf_t* child_loc_conf = (ngx_http_echo_loc_conf_t*)child;

    ngx_conf_merge_str_value(child_loc_conf->echo_msg, parent_loc_conf->echo_msg, "");

    return NGX_CONF_OK;
}

static char* ngx_http_echo_cmd(ngx_conf_t* conf_ctx, ngx_command_t* cmd, void* conf)
{
    ngx_http_core_loc_conf_t* core_loc_conf;

    /* Parses the directive argument and stores it into echo_msg */
    char* config_result = ngx_conf_set_str_slot(conf_ctx, cmd, conf);
    if (config_result != NGX_CONF_OK) {
        return config_result;
    }

    /* Sets the request handler for locations using this directive */
    core_loc_conf = (ngx_http_core_loc_conf_t*)ngx_http_conf_get_module_loc_conf(
        conf_ctx, ngx_http_core_module);
    core_loc_conf->handler = ngx_http_echo_handler;

    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_echo_handler(ngx_http_request_t* request)
{
    ngx_int_t status_code;
    ngx_buf_t* response_buf;
    ngx_chain_t output_chain;
    ngx_http_echo_loc_conf_t* module_loc_conf;

    if (!(request->method & (NGX_HTTP_GET | NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    status_code = ngx_http_discard_request_body(request);
    if (status_code != NGX_OK) {
        return status_code;
    }

    module_loc_conf = (ngx_http_echo_loc_conf_t*)ngx_http_get_module_loc_conf(
        request, ngx_request_coalescing_module);

    /* Headers setup */
    request->headers_out.content_type_len = sizeof("text/plain") - 1;
    ngx_str_set(&request->headers_out.content_type, "text/plain");
    request->headers_out.status = NGX_HTTP_OK;
    request->headers_out.content_length_n = module_loc_conf->echo_msg.len;

    if (request->method == NGX_HTTP_HEAD) {
        return ngx_http_send_header(request);
    }

    /* Buffer allocation */
    response_buf = ngx_create_temp_buf(request->pool, module_loc_conf->echo_msg.len);
    if (response_buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_memcpy(response_buf->pos, module_loc_conf->echo_msg.data, module_loc_conf->echo_msg.len);
    response_buf->last = response_buf->pos + module_loc_conf->echo_msg.len;
    response_buf->last_buf = 1;
    response_buf->last_in_chain = 1;

    output_chain.buf = response_buf;
    output_chain.next = NULL;

    status_code = ngx_http_send_header(request);
    if (status_code == NGX_ERROR || status_code > NGX_OK || request->header_only) {
        return status_code;
    }

    return ngx_http_output_filter(request, &output_chain);
}