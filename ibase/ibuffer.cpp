#include "ibuffer.hpp"
#include <utility>

namespace ibase {

    ibuffer::ibuffer(uint8_t* buf, uint32_t len)
    : buf_(buf)
    , len_(len)
    {
        if (buf_ == nullptr && len_ > 0)
        {
            buf_ = new uint8_t[len_];
        }
    }

    ibuffer::ibuffer(ibuffer&& other)
    : buf_(nullptr)
    , len_(0)
    {
        other.swap(*this);
    }

    ibuffer::~ibuffer()
    {
        delete[] buf_;
    }

    ibuffer& ibuffer::operator=(ibuffer&& other)
    {
        other.swap(*this);
        return *this;
    }

    uint8_t* ibuffer::buf()
    {
        return buf_;
    }

    uint32_t ibuffer::len()
    {
        return len_;
    }

    void ibuffer::swap(ibuffer& other) noexcept
    {
        std::swap(buf_, other.buf_);
        std::swap(len_,  other.len_);
    }
}
