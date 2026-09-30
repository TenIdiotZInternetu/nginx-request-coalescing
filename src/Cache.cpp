#include "Cache.hpp"

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
    uint32_t tag_mask = ~rings_idx_mask_;
    return {                                 //
        .ring_idx = hash & rings_idx_mask_,  //
        .tag = hash & tag_mask};
}


Cache* Cache::init(ngx_conf_t* config_ctx, size_t rings_count, size_t rings_size)
{
    Cache cache;
    cache.rings_count_ = rings_count;
    cache.rings_size_ = rings_size;
    cache.rings_idx_mask_ = align_to_nearest_exp(rings_count) - 1;

    ngx_shm_zone_t* shm_zone = ngx_shared_memory_add(
        config_ctx, SHM_ZONE_NAME, cache.total_cache_size(), void* tag);

    ngx_slab_alloc(, size_t size);

    cache.data_ = shm_zone->data;
    ngx_memcpy(&cache, cache.data_, sizeof(Cache));
    return (Cache*)cache.data_;
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
    ring_head_t* first_ring = (ring_head_t*)((Cache*)data_ + 1);
    return first_ring + ring_idx;
}

Cache::ring_entry_t* Cache::get_ring_entry(ring_head_t* ring, uint32_t tag)
{
    ring_entry_t* entry;
    ring_entry_t* first_entry = (ring_entry_t*)(ring + 1);

    for (uint32_t entry_idx = 0; entry_idx < rings_size_; ++entry_idx) {
        entry = first_entry + entry_idx;
        if (entry->tag == tag) {
            return entry;
        }
    }

    return NULL;
}


}  // namespace ngx::http::coalesce
