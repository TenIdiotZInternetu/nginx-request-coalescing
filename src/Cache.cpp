#include "Cache.hpp"

#include <sys/types.h>

#include <cstdint>

#include "ngx_config.h"
#include "ngx_core.h"

extern "C" {
#include "ngx_cycle.h"
#include "ngx_shmtx.h"
#include "ngx_slab.h"
#include "ngx_string.h"
}


namespace ngx::http::coalesce  //
{

Cache::hash_t Cache::hash_key(ngx_str_t key)
{
    uint32_t hash = ngx_murmur_hash2(key.data, key.len);
    uint32_t rings_idx_mask = rings_count_ - 1;
    uint32_t tag_mask = ~rings_idx_mask;
    return {                                //
        .ring_idx = hash & rings_idx_mask,  //
        .tag = hash & tag_mask};
}


ngx_int_t Cache::init(ngx_conf_t* config_ctx, size_t rings_count, size_t rings_size, void* tag)
{
    Cache* cache = (Cache*)ngx_pcalloc(config_ctx->pool, sizeof(Cache));
    if (cache == NULL) {
        return NGX_ERROR;
    }

    cache->rings_count_ = align_to_nearest_exp(rings_count);
    cache->rings_size_ = rings_size;
    ngx_str_set(&cache->shm_zone_name_, SHM_ZONE_NAME);

    ngx_shm_zone_t* shm_zone = ngx_shared_memory_add(
        config_ctx, &cache->shm_zone_name_, cache->total_cache_size(), tag);
    if (shm_zone == NULL) {
        return NGX_ERROR;
    }

    shm_zone->data = cache;
    shm_zone->init = Cache::init_shm_zone;

    return NGX_OK;
}


ngx_int_t Cache::init_shm_zone(ngx_shm_zone_t* zone, void* old_data)
{
    if (old_data != NULL) {
        zone->data = old_data;
        return NGX_OK;
    }

    Cache* cache = (Cache*)zone->data;
    ngx_slab_pool_t* pool = (ngx_slab_pool_t*)zone->shm.addr;

    void* mem = ngx_slab_calloc(pool, cache->total_cache_size());
    if (mem == NULL) {
        return NGX_ERROR;
    }

    cache->data_ = mem;

    for (uint32_t i = 0; i < cache->rings_count(); ++i) {
        ring_head_t* ring = cache->get_ring_head(i);
        ngx_int_t res = ngx_shmtx_create(&ring->mutex, &ring->mutex_sh, (u_char*)SHM_ZONE_NAME);
        if (res != NGX_OK) {
            return NGX_ERROR;
        }
    }

    ngx_memcpy(mem, cache, sizeof(Cache));
    zone->data = mem;
    return NGX_OK;
}


Cache* Cache::open() {}


void Cache::set_entry_payload(ngx_str_t key, void* data, size_t data_size)
{
    hash_t hash = hash_key(key);
    ring_head_t* ring = get_ring_head(hash.ring_idx);

    ngx_shmtx_lock(&ring->mutex);
    ring_entry_t* entry = get_ring_entry(ring, hash.tag);
    entry->data = data;
    entry->data_size = data_size;
    ngx_shmtx_unlock(&ring->mutex);
}


void Cache::cpy_entry_payload(ngx_str_t key, void* destination)
{
    hash_t hash = hash_key(key);
    ring_head_t* ring = get_ring_head(hash.ring_idx);

    ngx_shmtx_lock(&ring->mutex);
    ring_entry_t* entry = get_ring_entry(ring, hash.tag);
    ngx_memcpy(destination, entry->data, entry->data_size);
    ngx_shmtx_unlock(&ring->mutex);
}

uint32_t Cache::align_to_nearest_exp(uint32_t num)
{
    uint32_t exp = 1;
    while (exp < num) {
        exp <<= 1;
    }

    return exp;
}

Cache::ring_head_t* Cache::get_ring_head(uint32_t ring_idx)
{
    u_char* addr = (u_char*)((Cache*)data_ + 1);
    addr += ring_idx * sizeof(ring_head_t) + rings_size_ * sizeof(ring_entry_t);
    return (ring_head_t*)addr;
}

Cache::ring_entry_t* Cache::get_ring_entry(ring_head_t* ring, uint32_t tag)
{
    ring_entry_t* entry;
    ring_entry_t* first_entry = (ring_entry_t*)(ring + 1);

    for (uint32_t entry_idx = 0; entry_idx < rings_size_; ++entry_idx) {
        entry = first_entry + entry_idx;
        if (entry->tag == tag && tag != 0) {
            return entry;
        }
    }

    return NULL;
}


}  // namespace ngx::http::coalesce
