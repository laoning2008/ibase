#pragma once
#include <memory>

namespace ibase
{
    class packet_t
    {
        #pragma pack(1)
        struct packet_header_t
        {
            uint8_t         flag;
            uint32_t        cmd;
            uint32_t        seq;
            uint8_t         is_push;
            uint32_t        body_len;
            uint8_t         crc;
        };
        #pragma pack()
        
        constexpr static uint8_t packet_begin_flag = 0x55;
        constexpr static uint32_t header_length = sizeof(packet_header_t);
        constexpr static uint32_t max_packet_length = 16*1024;
        constexpr static uint32_t max_body_length = max_packet_length - header_length;
    public:
        static std::shared_ptr<packet_t> build_packet(uint32_t cmd, uint32_t seq, bool is_push, uint8_t* body_buf, uint32_t body_len);
        static std::shared_ptr<packet_t> parse_packet(uint8_t* buf, uint32_t buf_len, uint32_t& consume_len);
        
        packet_t(uint32_t cmd, uint32_t seq, bool is_push, packet_header_t& header, uint8_t* body_buf, uint32_t body_len);

        uint32_t cmd();
        uint32_t seq();
        bool is_push();
        const uint8_t* body() const;
        uint8_t* body();
        uint32_t body_length() const;
        const uint8_t* data() const;
        uint8_t* data();
        uint32_t length() const;
    private:
        packet_t(const packet_t& other) = delete;
        packet_t(packet_t&& other) = delete;
        packet_t& operator=(const packet_t& other) = delete;
        packet_t& operator=(packet_t&& other) = delete;
    private:
        static uint8_t calc_crc8(const uint8_t* data, uint32_t len);
      private:
        uint8_t         data_[max_packet_length] = {0};
        uint32_t        cmd_{0};
        uint32_t        seq_{0};
        uint8_t         is_push_{0};
        uint32_t        body_length_{0};
    };
}
