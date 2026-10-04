#include "Cache.hpp"

#include <sys/types.h>

#include <cstdint>


extern "C" {
#include "ngx_alloc.h"
#include "ngx_config.h"
#include "ngx_core.h"
#include "ngx_cycle.h"
#include "ngx_shmtx.h"
#include "ngx_slab.h"
#include "ngx_string.h"
}


namespace ngx::http::coalesce  //
{

uint32_t Cache::s_zone_id_ = 0;

Cache::hash_t Cache::hash_key(ngx_str_t key)
{
    uint32_t hash = ngx_murmur_hash2(key.data, key.len);
    uint32_t rings_idx_mask = rings_count_ - 1;
    uint32_t tag_mask = ~rings_idx_mask;
    return {                                //
        .ring_idx = hash & rings_idx_mask,  //
        .tag = hash & tag_mask};
}


ngx_shm_zone_t* Cache::init(ngx_conf_t* config_ctx, uint32_t rings_count, uint32_t rings_size,
    uint32_t slot_size, void* tag)
{
    Cache* cache = (Cache*)ngx_pcalloc(config_ctx->pool, sizeof(Cache));
    if (cache == NULL) {
        return NULL;
    }

    cache->rings_count_ = align_to_nearest_exp(rings_count);
    cache->rings_size_ = rings_size;
    cache->slot_size_ = ngx_align(slot_size, NGX_ALIGNMENT);

    uint32_t name_len = sprintf(cache->shm_zone_name_, "%s_%u", SHM_ZONE_NAME, s_zone_id_);
    ngx_str_t zone_name = {(size_t)name_len, (u_char*)cache->shm_zone_name_};

    ngx_shm_zone_t* shm_zone = ngx_shared_memory_add(
        config_ctx, &zone_name, cache->total_cache_size(), tag);
    if (shm_zone == NULL) {
        return NULL;
    }

    shm_zone->data = cache;
    shm_zone->init = Cache::init_shm_zone;

    ++s_zone_id_;
    return shm_zone;
}


ngx_int_t Cache::init_shm_zone(ngx_shm_zone_t* zone, void* old_data)
{
    if (old_data != NULL) {
        zone->data = old_data;
        return NGX_OK;
    }

    Cache* cache = (Cache*)zone->data;
    ngx_slab_pool_t* pool = (ngx_slab_pool_t*)zone->shm.addr;

    void* mem = ngx_slab_calloc(pool, cache->total_pool_size());
    if (mem == NULL) {
        return NGX_ERROR;
    }

    cache->addr_ = mem;
    if (init_rings(cache) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_memcpy(mem, cache, sizeof(Cache));
    zone->data = mem;
    return NGX_OK;
}


ngx_int_t Cache::init_rings(Cache* cache)
{
    u_char* slot_addr = cache->slots_begin();

    for (uint32_t i = 0; i < cache->rings_count(); ++i) {
        ring_head_t* ring = cache->get_ring_head(i);
        ngx_int_t res = ngx_shmtx_create(
            &ring->mutex, &ring->mutex_sh, (u_char*)cache->shm_zone_name_);

        if (res != NGX_OK) {
            return NGX_ERROR;
        }

        ring_entry_t* entries = (ring_entry_t*)(ring + 1);
        for (uint32_t j = 0; j < cache->rings_size(); ++j) {
            entries[j].payload = slot_addr;
            slot_addr += cache->slot_size_;
        }
    }

    return NGX_OK;
}


void Cache::set_entry_payload(ngx_str_t key, void* data, uint32_t data_size)
{
    hash_t hash = hash_key(key);
    ring_head_t* ring = get_ring_head(hash.ring_idx);

    ngx_shmtx_lock(&ring->mutex);
    ring_entry_t* entry = get_ring_entry(ring, hash.tag);
    ngx_memcpy(entry->payload, data, data_size);
    entry->payload_size = data_size;
    ngx_shmtx_unlock(&ring->mutex);
}


void Cache::cpy_entry_payload(ngx_str_t key, void* destination)
{
    hash_t hash = hash_key(key);
    ring_head_t* ring = get_ring_head(hash.ring_idx);

    ngx_shmtx_lock(&ring->mutex);
    ring_entry_t* entry = get_ring_entry(ring, hash.tag);
    ngx_memcpy(destination, entry->payload, entry->payload_size);
    ngx_shmtx_unlock(&ring->mutex);
}


size_t Cache::total_cache_size()
{
    size_t data = total_pool_size();
    size_t pages = data / ngx_pagesize;
    size_t overhead_leeway = 4 * ngx_pagesize;

    size_t slab_overhead = pages * sizeof(ngx_slab_page_t) + overhead_leeway;
    return data + slab_overhead;
}


size_t Cache::total_pool_size()
{
    size_t entry_size = sizeof(ring_entry_t) + slot_size_;
    return sizeof(Cache) + rings_count_ * (sizeof(ring_head_t) + rings_size_ * entry_size);
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
    u_char* addr = (u_char*)((Cache*)addr_ + 1);
    addr += ring_idx * (sizeof(ring_head_t) + rings_size_ * sizeof(ring_entry_t));
    return (ring_head_t*)addr;
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


u_char* Cache::slots_begin()
{
    u_char* rings_end = (u_char*)get_ring_head(rings_count_);
    return ngx_align_ptr(rings_end, NGX_ALIGNMENT);
}
}  // namespace ngx::http::coalesce
