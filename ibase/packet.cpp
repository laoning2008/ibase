#include "packet.hpp"
#include <asio.hpp>

namespace ibase
{
    static const uint8_t crc8_table[] =
    {
        0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
        0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
        0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
        0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
        0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
        0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
        0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
        0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
        0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
        0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
        0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
        0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
        0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
        0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
        0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
        0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35,
    };

    std::shared_ptr<packet_t> packet_t::build_packet(uint32_t cmd, uint32_t seq, bool is_push, uint8_t* body_buf, uint32_t body_len)
    {
        if (header_length + body_len > max_body_length)
        {
            return nullptr;
        }
        
        packet_header_t header;
        header.flag = packet_begin_flag;
        header.cmd = asio::detail::socket_ops::host_to_network_long(cmd);
        header.seq = asio::detail::socket_ops::host_to_network_long(seq);
        header.is_push = is_push?1:0;
        header.body_len = asio::detail::socket_ops::host_to_network_long(body_len);
        header.crc = calc_crc8((const uint8_t*)&header, header_length - 1);
        
        return std::make_shared<packet_t>(cmd, seq, is_push, header, body_buf, body_len);
    }

    std::shared_ptr<packet_t> packet_t::parse_packet(uint8_t* buf, uint32_t buf_len, uint32_t& consume_len)
    {
        consume_len = 0;
        do {
            for (; consume_len < buf_len; ++consume_len)
            {
                if (buf[consume_len] == packet_begin_flag)
                {
                    break;
                }
            }
            
            uint8_t* buf_valid = buf + consume_len;
            uint32_t buf_valid_len = buf_len - consume_len;
            
            if (buf_valid_len < header_length)
            {
                return nullptr;
            }
            
            packet_header_t* header = (packet_header_t*)buf_valid;
            auto crc = calc_crc8(buf_valid, header_length - 1);
            if (crc != header->crc)
            {
                ++consume_len;
                continue;
            }
            
            uint32_t body_len = asio::detail::socket_ops::network_to_host_long(header->body_len);
            
            if (body_len > max_body_length)
            {
                consume_len += header_length;
                continue;
            }
            
            if (buf_valid_len < header_length + body_len)
            {
                return nullptr;
            }
            consume_len += header_length + body_len;

            uint32_t cmd = asio::detail::socket_ops::network_to_host_long(header->cmd);
            uint32_t seq = asio::detail::socket_ops::network_to_host_long(header->seq);
            bool is_push = (header->is_push == 1)?true:false;
            return std::make_shared<packet_t>(cmd, seq, is_push, *header, buf_valid + header_length, body_len);
        } while (1);
        
        return nullptr;
    }
        
    packet_t::packet_t(uint32_t cmd, uint32_t seq, bool is_push, packet_header_t& header, uint8_t* body_buf, uint32_t body_len)
        : cmd_(cmd)
        , seq_(seq)
        , is_push_(is_push?1:0)
        , body_length_(body_len)
    {
        memcpy(data_, &header, header_length);
        if ((body_buf != nullptr) && (body_len > 0))
        {
            memcpy(data_ + header_length, body_buf, body_len);
        }
    }

    uint32_t packet_t::cmd()
    {
        return cmd_;
    }
          
    uint32_t packet_t::seq() 
    {
        return seq_;
    }

    bool packet_t::is_push()
    {
        return is_push_ != 0;
    }

    const uint8_t* packet_t::body() const
    {
        return data_ + header_length;
    }

    uint8_t* packet_t::body()
    {
        return data_ + header_length;
    }

    uint32_t packet_t::body_length() const
    {
        return body_length_;
    }

    const uint8_t* packet_t::data() const
    {
        return data_;
    }

    uint8_t* packet_t::data() {
        return data_;
    }

    uint32_t packet_t::length() const
    {
        return header_length + body_length_;
    }

    uint8_t packet_t::calc_crc8(const uint8_t* data, uint32_t len)
    {
        unsigned char val = 0x77;
        for (int i = 0; i < len; i++)
        {
            val = val ^ data[i];
            val = crc8_table[val];
        }
        return val;
    }
}
