#ifndef __NGX_REQUEST_COALESCING_CACHE__HPP
#define __NGX_REQUEST_COALESCING_CACHE__HPP

#include <cstdint>


extern "C" {
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_shmtx.h"
}

namespace ngx::http::coalesce {

class Cache {
   public:
    struct ring_head_t {
        uint8_t writer_idx;
        ngx_shmtx_sh_t mutex_sh;
        ngx_shmtx_t mutex;
    };

    struct ring_entry_t {
        void* data;
        size_t data_size;
        uint32_t tag;
        bool completed;
    };

    struct hash_t {
        uint32_t ring_idx;
        uint32_t tag;
    };

    static constexpr char SHM_ZONE_NAME[] = "REQUEST_COALESCING_RING_BUFFER_CACHE";

    static ngx_int_t init(ngx_conf_t* cf, size_t rings_count, size_t rings_size, void* tag);
    static Cache* open();

    bool key_exists(ngx_str_t key);
    void add_entry(ngx_str_t key);
    void remove_entry(ngx_str_t key);

    void set_entry_payload(ngx_str_t key, void* data, size_t data_size);
    void cpy_entry_payload(ngx_str_t key, void* destination);

    uint32_t rings_count() { return rings_count_; }
    uint32_t rings_size() { return rings_size_; }
    uint32_t total_cache_size()  // TODO: align
    {
        return rings_count_ * (rings_size_ * sizeof(ring_entry_t) + sizeof(ring_head_t))
             + sizeof(Cache);
    }

   private:
    ngx_str_t shm_zone_name_;
    void* data_;
    uint32_t rings_count_;
    uint32_t rings_size_;

    hash_t hash_key(ngx_str_t key);
    static ngx_int_t init_shm_zone(ngx_shm_zone_t* zone, void* cache_data);

    ring_head_t* get_ring_head(uint32_t ring_idx);
    ring_entry_t* get_ring_entry(ring_head_t* ring, uint32_t tag);
    static uint32_t align_to_nearest_exp(uint32_t num);
};

}  // namespace ngx::http::coalesce

#endif