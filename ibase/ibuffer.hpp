#pragma once
#include <stdint.h>

namespace ibase
{
    class ibuffer
    {
    public:
        //be carefull, it takes ownership of buf. buf should be allocate by new[]
        ibuffer(uint8_t* buf, uint32_t len);
        ibuffer(const ibuffer& other) = delete;
        ibuffer(ibuffer&& other);
        ~ibuffer();
    
        ibuffer& operator=(const ibuffer& other) = delete;
        ibuffer& operator=(ibuffer&& other);
        
        uint8_t* buf();
        uint32_t len();
    private:
        void swap(ibuffer& other) noexcept;
    private:
        uint8_t* buf_;
        uint32_t len_;
    };
}
