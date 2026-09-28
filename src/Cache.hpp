#ifndef __NGX_REQUEST_COALESCING_CACHE__HPP
#define __NGX_REQUEST_COALESCING_CACHE__HPP

#include <cstdint>

#include "ngx_shmtx.h"

extern "C" {
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace ngx::http::coalesce {

class Cache {
   public:
    struct ring_head_t {
        uint8_t writer_idx;
        ngx_shmtx_t mutex;
    };

    struct ring_entry_t {
        uint32_t tag;
        void* data;
    };

    struct hash_t {
        uint32_t ring_idx;
        uint32_t tag;
    };

    static constexpr char SHM_ZONE_NAME[] = "REQUEST_COALESCING_RING_BUFFER_CACHE";

    static Cache* init(ngx_conf_t* config_ctx, size_t rings_count, size_t rings_size);
    static Cache* open();

    bool key_exists(ngx_str_t key) { return get_entry(hash_key(key)) == NULL; };
    void add_entry(ngx_str_t key);
    void remove_entry(ngx_str_t key);

    void set_entry_payload(ngx_str_t key, void* data);
    void* get_entry_payload(ngx_str_t key);

    uint32_t rings_count() { return rings_count_; }
    uint32_t rings_size() { return rings_size_; }
    uint32_t total_cache_size()  // TODO: align
    {
        return rings_count_ * (rings_size_ * sizeof(ring_entry_t) + sizeof(ring_head_t))
             + sizeof(Cache);
    }

   private:
    void* data_;
    uint32_t rings_count_;
    uint32_t rings_size_;
    uint32_t rings_idx_mask_;

    hash_t hash_key(ngx_str_t key);
    ring_head_t* get_ring_head(uint32_t ring_idx);
    ring_entry_t* get_ring_entry(ring_head_t* ring, uint32_t tag);
    static uint32_t align_to_nearest_exp(uint32_t num);
};

}  // namespace ngx::http::coalesce

#endif