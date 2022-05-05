#include "reliable_tcp_session.hpp"
#include "reliable_tcp_server.hpp"
#include <assert.h>
#include <fmt/core.h>
#include "ilogger.hpp"
#include "task_runner.hpp"

namespace ibase
{
    reliable_tcp_session_t::reliable_tcp_session_t(uint32_t session_id, asio::ip::tcp::socket socket, asio::io_context& io_context, receive_packet_callback_t receive_packet_callback)
    : io_context_(io_context)
    , session_id_(session_id)
    , receive_packet_callback_(receive_packet_callback)
    , socket_(std::move(socket))
    , read_pending_(false)
    , read_buf_(max_read_buffer_size)
    , timer_(std::make_shared<itimer>(io_context))
    {
    }

    reliable_tcp_session_t::~reliable_tcp_session_t()
    {
    }

    void reliable_tcp_session_t::start()
    {
        do_start();
    }

    void reliable_tcp_session_t::send_packet(std::shared_ptr<packet_t> packet)
    {
        if (packet->is_push())
        {
            write_packets_.push_back({packet, 1, std::chrono::steady_clock::now()});
            do_write_packet(packet);
        }
        else
        {
            do_write_packet(packet);
        }
    }

    uint32_t reliable_tcp_session_t::get_session_id()
    {
        return session_id_;
    }

    void reliable_tcp_session_t::do_start()
    {
        check_timer_id_ = timer_->start_timer(std::bind(&reliable_tcp_session_t::on_priodically_timer, this), 1, 1);
        do_read_packet();
    }

    void reliable_tcp_session_t::do_stop()
    {
        ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("server session do_close"));

        timer_->stop_timer(check_timer_id_);
        check_timer_id_ = 0;
        
        read_buf_.clear();
        write_packets_.clear();
        rencently_packet_tracker_.clear();
        
        read_pending_ = false;
        if (socket_.is_open())
        {
            ibase::logger::write_log(ibase::logger::log_level_debug, "server session really do_close");
            asio::error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_both, ec);
            socket_.close(ec);
        }
    }

    void reliable_tcp_session_t::do_read_packet()
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
        
        auto buf = read_buf_.prepare(size_to_read);
        read_pending_ = true;
        
        std::weak_ptr<reliable_tcp_session_t> weak_this(shared_from_this());
        socket_.async_read_some(asio::buffer(buf.data, buf.size),
          [weak_this](std::error_code ec, std::size_t length)
          {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->read_pending_ = false;

            if (!ec)
            {
                shared_this->process_read_data(length);
            }
            
            //other cases, let session_mgr timeout check do it's job
          });
    }

    void reliable_tcp_session_t::do_write_packet(const std::shared_ptr<packet_t> packet)
    {
        if (!is_connected())
        {
            return;
        }
        
        std::weak_ptr<reliable_tcp_session_t> weak_this(shared_from_this());
        asio::async_write(socket_, asio::buffer(packet->data(), packet->length()),
          [weak_this](std::error_code ec, std::size_t length)
          {
            //we don't care. next packet sending is driven by timer
          });
    }

    void reliable_tcp_session_t::process_read_data(uint32_t read_data_size)
    {
        if (read_data_size > 0)
        {
            read_buf_.commit(read_data_size);
            process_packet();
        }

        do_read_packet();
    }

    void reliable_tcp_session_t::process_packet() {
        do
        {
            uint32_t consume_len = 0;
            auto packet = packet_t::parse_packet(read_buf_.read_head(), read_buf_.size(), consume_len);
            read_buf_.consume(consume_len);
            
            if (!packet)
            {
                break;
            }
            
            ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("server recv packet, cmd =  {}, seq = {}", packet->cmd(), packet->seq()));

            //dispatch packet
            if (packet->is_push())
            {
                process_push_packet(packet);
            }
            else
            {
                process_request_packet(packet);
            }
            
            
        } while (1);
    }

    void reliable_tcp_session_t::process_request_packet(const std::shared_ptr<packet_t> packet)
    {
        auto duplicate = rencently_packet_tracker_.on_receive_packet(packet->cmd(), packet->seq());
        if (duplicate)
        {
            return;
        }
        
        receive_packet_callback_(session_id_, packet);
    }

    void reliable_tcp_session_t::process_push_packet(const std::shared_ptr<packet_t> packet)
    {
        for (auto it = write_packets_.begin(); it != write_packets_.end(); ++it)
        {
            if ((it->packet_->cmd() == packet->cmd()) && (it->packet_->seq() == packet->seq()))
            {
                write_packets_.erase(it);
                break;
            }
        }

        receive_packet_callback_(session_id_, packet);
    }

    bool reliable_tcp_session_t::is_connected()
    {
        return socket_.is_open();
    }

    void reliable_tcp_session_t::on_priodically_timer()
    {
        auto cur_timepoint_ = std::chrono::steady_clock::now();
        do_resender_check(cur_timepoint_);
    }

    void reliable_tcp_session_t::do_resender_check(const std::chrono::steady_clock::time_point& cur_time_point)
    {
        for (auto it = write_packets_.begin(); it != write_packets_.end(); )
        {
            auto time_passed_by_seconds = std::chrono::duration_cast<std::chrono::seconds>(cur_time_point - it->last_send_time_point_);

            if (time_passed_by_seconds.count() < resend_interval_in_seconds)
            {
                ++it;
                continue;
            }

            if (it->cur_tries_ >= max_resend_tries)
            {
                it = write_packets_.erase(it);
                continue;
            }

            ++it->cur_tries_;
            it->last_send_time_point_ = cur_time_point;
            do_write_packet(it->packet_);

            ++it;
        }
    }
}
