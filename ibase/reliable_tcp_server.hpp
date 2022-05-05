#pragma once
#include <map>
#include <memory>
#include <asio.hpp>
#include "packet.hpp"
#include "itimer.hpp"

namespace ibase
{
    class reliable_tcp_session_t;

    //thread safe
    class reliable_tcp_server_t : public std::enable_shared_from_this<reliable_tcp_server_t>
    {
        struct session_info
        {
            std::shared_ptr<reliable_tcp_session_t> session_;
            std::chrono::steady_clock::time_point last_recv_timepoint_;
        };
        
        using map_session_id_2_session_t = std::map<uint32_t, session_info>;
        constexpr static uint32_t max_heartbeat_interval_seconds = 20;
        
    public:
        using req_processor_t = std::function<void(uint32_t session_id, std::shared_ptr<packet_t> packet)>;
        using map_req_2_processor_t = std::map<uint32_t, req_processor_t>;
    public:
        reliable_tcp_server_t(asio::io_context& io_context, const uint16_t port);
        ~reliable_tcp_server_t();
        reliable_tcp_server_t(const reliable_tcp_server_t& other) = delete;
        reliable_tcp_server_t(reliable_tcp_server_t&& other) = delete;
        reliable_tcp_server_t& operator=(const reliable_tcp_server_t& other) = delete;
        reliable_tcp_server_t& operator=(reliable_tcp_server_t&& other) = delete;

    public:
        bool start();
        void stop();
        bool started();
        
        void register_req_processor(uint32_t cmd, req_processor_t processor);
        void unregister_req_processor(uint32_t cmd);
        
        bool send_rsp_for_req(uint32_t session_id, uint32_t cmd, uint32_t seq, uint8_t* rsp_buf, uint32_t rsp_len);
        bool publish_notification(uint32_t cmd, uint8_t* notification_buf, uint32_t notification_len);
    private:
        bool start_impl();
        void stop_impl();
        void register_req_processor_impl(uint32_t cmd, req_processor_t processor);
        void unregister_req_processor_impl(uint32_t cmd);
        bool send_rsp_for_req_impl(uint32_t session_id, uint32_t cmd, uint32_t seq, uint8_t* rsp_buf, uint32_t rsp_len);
        bool publish_notification_impl(uint32_t cmd, uint8_t* notification_buf, uint32_t notification_len);
    private:
        void add_new_session(asio::ip::tcp::socket socket);
        std::shared_ptr<reliable_tcp_session_t> get_session(uint32_t session_id);
        void on_heartbeat(uint32_t session_id);
        void on_priodically_timer();
    private:
        void start_accept();
        void do_close();
        void do_accept();
        void dispatch_packet(uint32_t session_id, std::shared_ptr<packet_t> packet);
        bool send_packet(uint32_t session_id, uint32_t cmd, uint32_t seq, bool is_push, uint8_t* rsp_buf, uint32_t rsp_len);
    private:
        asio::io_context&                                           io_context_;
        asio::ip::tcp::acceptor                                     acceptor_;
        uint16_t                                                    port_{0};
        map_req_2_processor_t                                       req_2_processor_;
        volatile std::atomic<bool>                                  started_ {false};

        map_session_id_2_session_t                                  sessions_;
        std::atomic<uint32_t>                                       cur_session_id{0};
        std::atomic<uint32_t>                                       cur_seq_{0};
        
        //timer
        std::shared_ptr<itimer>                                     timer_;
        uint32_t                                                    check_timer_id_{0};        
    };
}
