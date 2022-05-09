#include "reliable_tcp_client.hpp"
#include "task_runner.hpp"
#include <fmt/core.h>
#include "ilogger.hpp"

namespace ibase
{
    reliable_tcp_client_t::send_opt_t reliable_tcp_client_t::default_send_opt{3, 3};

    reliable_tcp_client_t::reliable_tcp_client_t(asio::io_context& io_context)
    : io_context_(io_context)
    , socket_(io_context)
    , read_buf_(max_read_buffer_size)
    , timer_(std::make_shared<itimer>(io_context))
    , connect_state_(connect_state_t::disconnected)
    {
    }

    reliable_tcp_client_t::~reliable_tcp_client_t()
    {
    }

    bool reliable_tcp_client_t::start(std::string host, const uint16_t port)
    {
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        return ibase::task::run_task_in_the_iocontext_sync<bool>(io_context_, [weak_this, host, port]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return false;
            }

            return shared_this->start_impl(host, port);
        });
    }

    bool reliable_tcp_client_t::start_impl(std::string host, const uint16_t port)
    {
        if (started_)
        {
            return true;
        }
        started_ = true;
        host_ = host;
        port_ = port;

        do_connect();
        check_timer_id_ = timer_->start_timer(std::bind(&reliable_tcp_client_t::on_priodically_timer, this), 1, 1);
        return check_timer_id_ != 0;
    }

    void reliable_tcp_client_t::stop()
    {
        if (!started_)
        {
            return;
        }
        
        started_ = false;
        
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_sync<void>(io_context_, [weak_this]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }
          
            shared_this->stop_impl();
        });
    }

    void reliable_tcp_client_t::stop_impl()
    {
        timer_->stop_timer(check_timer_id_);
        check_timer_id_ = 0;

        do_close();

        read_buf_.clear();
        write_packets_.clear();
        notifications_.clear();
        rencently_packet_tracker_.clear();

        last_connect_timepoint_ = std::chrono::steady_clock::time_point();
        last_heartbeat_timepoint_ = std::chrono::steady_clock::time_point();
    }

    bool reliable_tcp_client_t::started()
    {
        return started_;
    }

    uint32_t reliable_tcp_client_t::send_req_async(uint32_t cmd, uint8_t* req_buf, uint32_t req_len, send_opt_t* opt, send_callback_t callback)
    {
        auto packet = packet_t::build_packet(cmd, ++cur_seq_, false, req_buf, req_len);
        if (packet == nullptr)
        {
            return 0;
        }

        if (opt == nullptr)
        {
            opt = &default_send_opt;
        }

        auto send_id = ++cur_send_id_;
        auto opt_copy = *opt;
        
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, packet, opt_copy, send_id, callback]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->send_req_async_impl(packet, send_id, opt_copy, callback);
        });

        return send_id;
    }

    void reliable_tcp_client_t::send_req_async_impl(std::shared_ptr<packet_t> packet, uint32_t send_id, send_opt_t opt, send_callback_t callback)
    {
        write_packets_.push_back({ packet, opt, send_id, callback, 1, std::chrono::steady_clock::now() });
        do_write_packet(packet);
    }

    void reliable_tcp_client_t::send_cancel(uint32_t send_id)
    {
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, send_id]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->send_cancel_impl(send_id);
        });
    }

    void reliable_tcp_client_t::send_cancel_impl(uint32_t send_id)
    {
        for (auto it = write_packets_.begin(); it != write_packets_.end(); ++it)
        {
            if (it->send_id_ == send_id)
            {
                write_packets_.erase(it);
                break;
            }
        }
    }

    void reliable_tcp_client_t::subscribe_notification(uint32_t cmd, notification_callback_t callback)
    {
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());

        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, cmd, callback]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->subscribe_notification_impl(cmd, callback);
        });
    }

    void reliable_tcp_client_t::subscribe_notification_impl(uint32_t cmd, notification_callback_t callback)
    {
        notifications_[cmd] = callback;
    }

    void reliable_tcp_client_t::unsubscribe_notification(uint32_t cmd)
    {
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, cmd]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->unsubscribe_notification_impl(cmd);
        });
    }

    void reliable_tcp_client_t::unsubscribe_notification_impl(uint32_t cmd)
    {
        notifications_.erase(cmd);
    }

    void reliable_tcp_client_t::do_close()
    {
        if (connect_state_ == connect_state_t::disconnected)
        {
            return;
        }

        ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("client do_close"));

        read_pending_ = false;
        connect_state_ = connect_state_t::disconnected;
        if (socket_.is_open())
        {
            ibase::logger::write_log(ibase::logger::log_level_debug, "client really do_close");
            asio::error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_both, ec);
            socket_.close(ec);
        }
    }

    void reliable_tcp_client_t::do_connect()
    {
        if (!started())
        {
            return;
        }
        
        asio::error_code ec;
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoint = resolver.resolve(host_, std::to_string(port_), ec);
        
        last_connect_timepoint_ = std::chrono::steady_clock::now();
        connect_state_ = connect_state_t::connecting;

        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        asio::async_connect(socket_, endpoint,
               [weak_this](std::error_code ec, asio::ip::tcp::endpoint)
        {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            if (ec)
            {
                shared_this->connect_state_ = connect_state_t::disconnected;
                return;
            }

            shared_this->on_connected();
        });
    }

    void reliable_tcp_client_t::do_read_packet()
    {
        if (!is_connected())
        {
            return;
        }
        
        if (read_pending_)
        {
            return;
        }
        
        auto size_to_read = (read_buf_.free_size() > 0) ? read_buf_.free_size() : read_buf_.capacity();
        if (size_to_read <= 0)
        {
            return;
        }

        read_pending_ = true;
        auto buf = read_buf_.prepare(size_to_read);

        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        socket_.async_read_some(asio::buffer(buf.data, buf.size), [weak_this](std::error_code ec, std::size_t length) {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->read_pending_ = false;
            
            if (ec)
            {
                shared_this->do_close();
            }
            else
            {
                shared_this->process_read_data(length);
            }
        });
    }

    void reliable_tcp_client_t::do_write_packet(const std::shared_ptr<packet_t> packet)
    {
        if (!packet)
        {
            return;
        }

        if (!is_connected())
        {
            return;
        }
        
        std::weak_ptr<reliable_tcp_client_t> weak_this(shared_from_this());
        asio::async_write(socket_, asio::buffer(packet->data(), packet->length()),
          [weak_this](std::error_code ec, std::size_t length)
          {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            if (ec)
            {
                shared_this->do_close();
            }
          });
    }


    void reliable_tcp_client_t::process_read_data(uint32_t read_data_size) {
        if (read_data_size > 0)
        {
            read_buf_.commit(read_data_size);
            process_packet();
        }

        do_read_packet();
    }

    void reliable_tcp_client_t::process_packet() {
        do {
            uint32_t consume_len = 0;
            auto packet = packet_t::parse_packet(read_buf_.read_head(), read_buf_.size(), consume_len);
            read_buf_.consume(consume_len);

            if (!packet)
            {
                break;
            }

            ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("client recv packet, cmd =  {}, seq = {}", packet->cmd(), packet->seq()));
            
            if (packet->is_push())
            {
                process_push_packet(packet);
            }
            else
            {
                process_response_packet(packet);
            }
        } while (1);
    }

    void reliable_tcp_client_t::process_response_packet(const std::shared_ptr<packet_t> packet)
    {
        for (auto it = write_packets_.begin(); it != write_packets_.end(); ++it)
        {
            if ((it->packet_->cmd() == packet->cmd()) && (it->packet_->seq() == packet->seq()))
            {
                sending_packet_info packet_info = *it;
                write_packets_.erase(it);

                do_send_req_callback(packet_info.callback_, packet_info.send_id_, 0, packet);
                break;
            }
        }
    }

    void reliable_tcp_client_t::process_push_packet(const std::shared_ptr<packet_t> packet)
    {
        ack_push_packet(packet);
        
        auto duplicate = rencently_packet_tracker_.on_receive_packet(packet->cmd(), packet->seq());
        if (!duplicate)
        {
            auto it = notifications_.find(packet->cmd());
            if (it != notifications_.end())
            {
                do_recv_notification_callback(it->second, packet);
            }
        }
    }

    void reliable_tcp_client_t::ack_push_packet(const std::shared_ptr<packet_t> packet)
    {
        if (!is_connected())
        {
            return;
        }

        auto rsp_packet = packet_t::build_packet(packet->cmd(), packet->seq(), true, nullptr, 0);
        do_write_packet(rsp_packet);
    }

    void reliable_tcp_client_t::on_priodically_timer()
    {
        auto cur_timepoint_ = std::chrono::steady_clock::now();

        do_reconnect_check(cur_timepoint_);
        do_resender_check(cur_timepoint_);
        do_heartbeat_check(cur_timepoint_);
    }


    void reliable_tcp_client_t::do_reconnect_check(const std::chrono::steady_clock::time_point& cur_time_point)
    {
        if (connect_state_ == connect_state_t::connected)
        {
            return;
        }

        if (connect_state_ == connect_state_t::connecting)
        {
            return;
        }

        auto time_passed_by_seconds = std::chrono::duration_cast<std::chrono::seconds>(cur_time_point - last_connect_timepoint_);
        if (time_passed_by_seconds.count() < reconnect_interval_seconds)
        {
            return;
        }

        do_connect();
    }

    void reliable_tcp_client_t::do_resender_check(const std::chrono::steady_clock::time_point& cur_time_point)
    {
        for (auto it = write_packets_.begin(); it != write_packets_.end(); )
        {
            auto time_passed_by_seconds = std::chrono::duration_cast<std::chrono::seconds>(cur_time_point - it->last_send_time_point_);

            if (time_passed_by_seconds.count() < it->send_opt_.interval_seconds)
            {
                ++it;
                continue;
            }

            if (it->cur_tries_ >= it->send_opt_.tries)
            {
                do_send_req_callback(it->callback_, it->send_id_, -1, it->packet_);
                it = write_packets_.erase(it);
                continue;
            }

            ++it->cur_tries_;
            it->last_send_time_point_ = cur_time_point;
            do_write_packet(it->packet_);

            ++it;
        }
    }

    void reliable_tcp_client_t::do_heartbeat_check(const std::chrono::steady_clock::time_point& cur_time_point)
    {
        auto time_passed_by_seconds = std::chrono::duration_cast<std::chrono::seconds>(cur_time_point - last_heartbeat_timepoint_);
        if (time_passed_by_seconds.count() < heartbeat_interval_seconds)
        {
            return;
        }

        if (!is_connected())
        {
            return;
        }

        auto packet = packet_t::build_packet(heartbeat_cmd, ++cur_seq_, true, nullptr, 0);
        do_write_packet(packet);
    }


    void reliable_tcp_client_t::on_connected()
    {
        connect_state_ = connect_state_t::connected;
        do_read_packet();
    }


    bool reliable_tcp_client_t::is_connected()
    {
        return connect_state_ == connect_state_t::connected;
    }

    bool reliable_tcp_client_t::is_connecting()
    {
        return connect_state_ == connect_state_t::connecting;
    }

    void reliable_tcp_client_t::do_send_req_callback(send_callback_t callback, uint32_t send_id, int result, std::shared_ptr<packet_t> packet)
    {
        callback(send_id, result, packet);
    }

    void reliable_tcp_client_t::do_recv_notification_callback(notification_callback_t callback, std::shared_ptr<packet_t> packet)
    {
        callback(packet);
    }
}
