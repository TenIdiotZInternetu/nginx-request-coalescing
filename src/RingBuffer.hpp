#ifndef __NGX_REQUEST_COALESCING_RINGBUFFER__HPP
#define __NGX_REQUEST_COALESCING_RINGBUFFER__HPP

#include <functional>

extern "C" {
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace ngx::http::coalesce {

template <typename T>
class RingBuffer {
   public:
    using evict_fnt = std::function<void(T)>;

    RingBuffer(std::size_t size, evict_fnt evict_callback)
        : size_(size), evict_callback_(evict_callback) {};

    std::size_t size() { return size_; }
    std::size_t writer_offset() { return writer_offset_; }

    //  std::size_t write(const T& item);

   private:
    std::size_t size_;
    std::size_t writer_offset_;
    evict_fnt evict_callback_;
};
}  // namespace ngx::http::coalesce

#endif