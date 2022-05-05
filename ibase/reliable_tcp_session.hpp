#pragma once
#include <asio.hpp>
#include <list>
#include <atomic>
#include "packet.hpp"
#include "io_buffer.hpp"
#include "itimer.hpp"
#include "recently_packet_tracker.hpp"

namespace ibase
{
    //this class should be used in reliable_tcp_server only!!!! not thread safe!!!!!!
    class reliable_tcp_session_t : public std::enable_shared_from_this<reliable_tcp_session_t>
    {
        struct sending_packet_info
        {
            std::shared_ptr<packet_t> packet_;
            uint32_t cur_tries_{0};
            std::chrono::steady_clock::time_point last_send_time_point_;
        };
        
        using packet_list_t = std::list<sending_packet_info>;
        constexpr static uint32_t max_read_buffer_size = 128*1024;
        constexpr static uint32_t max_resend_tries = 3;
        constexpr static uint32_t resend_interval_in_seconds = 3;

    public:
        using receive_packet_callback_t = std::function<void(uint32_t session_id, std::shared_ptr<packet_t> packet)>;
    public:
        reliable_tcp_session_t(uint32_t session_id, asio::ip::tcp::socket socket, asio::io_context& io_context, receive_packet_callback_t receive_packet_callback);
        ~reliable_tcp_session_t();
        
        void start();

        void send_packet(std::shared_ptr<packet_t> packet);
        uint32_t get_session_id();
    private:
        reliable_tcp_session_t(const reliable_tcp_session_t& other) = delete;
        void operator=(const reliable_tcp_session_t& other) = delete;
    private:
        void do_start();
        void do_stop();
        bool is_connected();

        void do_read_packet();
        void do_write_packet(const std::shared_ptr<packet_t> packet);
        void process_read_data(uint32_t read_data_size);
        void process_packet();
        void process_request_packet(const std::shared_ptr<packet_t> packet);
        void process_push_packet(const std::shared_ptr<packet_t> packet);
        
        
        void on_priodically_timer();
        void do_resender_check(const std::chrono::steady_clock::time_point& cur_time_point);
    private:
        asio::io_context&               io_context_;
        uint32_t                        session_id_;
        receive_packet_callback_t       receive_packet_callback_;
        asio::ip::tcp::socket           socket_;
        bool                            read_pending_;
        bev::io_buffer                  read_buf_;
        packet_list_t                   write_packets_;
        
        //timer
        std::shared_ptr<itimer>         timer_;
        uint32_t                        check_timer_id_{0};
        recently_packet_tracker_t       rencently_packet_tracker_;
    };
}
