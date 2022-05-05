#pragma once
#include <asio.hpp>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <vector>
#include "io_buffer.hpp"
#include "packet.hpp"
#include "itimer.hpp"
#include "recently_packet_tracker.hpp"

namespace ibase
{
    //thread safe
    class reliable_tcp_client_t : public std::enable_shared_from_this<reliable_tcp_client_t>
    {
    public:
        enum class connect_state_t
        {
            disconnected,
            connecting,
            connected,
        };
        
        using send_callback_t = std::function<void(uint32_t send_id, int result, std::shared_ptr<packet_t> packet)>;
        using notification_callback_t = std::function<void(std::shared_ptr<packet_t> packet)>;

        struct send_opt_t
        {
            uint32_t tries;
            uint32_t interval_seconds;
        };
        
        static send_opt_t default_send_opt;

        
        struct sending_packet_info
        {
            std::shared_ptr<packet_t> packet_;
            send_opt_t send_opt_;
            uint32_t send_id_{0};
            send_callback_t callback_;
            uint32_t cur_tries_{0};
            std::chrono::steady_clock::time_point last_send_time_point_;
        };
        
        using packet_list_t = std::list<sending_packet_info>;
        using map_cmd_2_notification_callback_t = std::map<uint32_t, notification_callback_t>;

        constexpr static uint32_t max_read_buffer_size = 128*1024;
        constexpr static uint32_t reconnect_interval_seconds = 5;
        constexpr static uint32_t heartbeat_interval_seconds = 5;

        constexpr static uint32_t heartbeat_cmd = 0;


    public:
        reliable_tcp_client_t(asio::io_context& io_context);
        ~reliable_tcp_client_t();
        reliable_tcp_client_t(const reliable_tcp_client_t& other) = delete;
        reliable_tcp_client_t(reliable_tcp_client_t&& other) = delete;
        reliable_tcp_client_t& operator=(const reliable_tcp_client_t& other) = delete;
        reliable_tcp_client_t& operator=(reliable_tcp_client_t&& other) = delete;
    public:
        bool start(std::string host, const uint16_t port);
        void stop();
        bool started();
        
        uint32_t send_req_async(uint32_t cmd, uint8_t*req_buf, uint32_t req_len, send_opt_t* opt, send_callback_t callback);
        void send_cancel(uint32_t send_id);

        void subscribe_notification(uint32_t cmd, notification_callback_t callback);
        void unsubscribe_notification(uint32_t cmd);

    private:
        bool start_impl(std::string host, const uint16_t port);
        void stop_impl();
        void send_req_async_impl(std::shared_ptr<packet_t> packet, uint32_t send_id, send_opt_t opt, send_callback_t callback);
        void send_cancel_impl(uint32_t send_id);
        void subscribe_notification_impl(uint32_t cmd, notification_callback_t callback);
        void unsubscribe_notification_impl(uint32_t cmd);
    private:
        void do_close();
        void do_connect();
        void do_read_packet();
        void do_write_packet(const std::shared_ptr<packet_t> packet);
        
        void process_read_data(uint32_t read_data_size);
        void process_packet();
        void process_response_packet(const std::shared_ptr<packet_t> packet);
        void process_push_packet(const std::shared_ptr<packet_t> packet);
        void ack_push_packet(const std::shared_ptr<packet_t> packet);
        
        void on_priodically_timer();
        void do_reconnect_check(const std::chrono::steady_clock::time_point& cur_time_point);
        void do_resender_check(const std::chrono::steady_clock::time_point& cur_time_point);
        void do_heartbeat_check(const std::chrono::steady_clock::time_point& cur_time_point);
        
        void on_connected();
        bool is_connected();
        bool is_connecting();
        
        void do_send_req_callback(send_callback_t callback, uint32_t send_id, int result, std::shared_ptr<packet_t> packet);
        void do_recv_notification_callback(notification_callback_t callback, std::shared_ptr<packet_t> packet);
    private:
        asio::io_context&                                           io_context_;
        asio::ip::tcp::socket                                       socket_;
        std::string                                                 host_;
        uint16_t                                                    port_{0};
        volatile std::atomic<bool>                                  started_ {false};
        
        //read need to sequence, because all reads use the same buffer
        bool                                                        read_pending_{false};
        bev::io_buffer                                              read_buf_;
        packet_list_t                                               write_packets_;
        map_cmd_2_notification_callback_t                           notifications_;
        
        std::atomic<uint32_t>                                       cur_seq_{0};
        std::atomic<uint32_t>                                       cur_send_id_{0};
        std::atomic<uint32_t>                                       cur_subscribe_id_{0};
        
        //timer
        std::shared_ptr<itimer>                                     timer_;
        uint32_t                                                    check_timer_id_{0};
        
        //connect
        connect_state_t                                             connect_state_;
        std::chrono::steady_clock::time_point                       last_connect_timepoint_;
        
        //heartbeat
        std::chrono::steady_clock::time_point                       last_heartbeat_timepoint_;
        
        
        recently_packet_tracker_t                                   rencently_packet_tracker_;
    };
}
