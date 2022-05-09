#include "reliable_tcp_server.hpp"
#include "reliable_tcp_session.hpp"
#include "task_runner.hpp"
#include "ilogger.hpp"
#include <fmt/core.h>

namespace ibase
{
    reliable_tcp_server_t::reliable_tcp_server_t(asio::io_context& io_context, const uint16_t port)
        : io_context_(io_context)
        , timer_(std::make_shared<itimer>(io_context))
        , port_(port)
        , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
        
    {
    }

    reliable_tcp_server_t::~reliable_tcp_server_t()
    {
    }


    bool reliable_tcp_server_t::start()
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        return ibase::task::run_task_in_the_iocontext_sync<bool>(io_context_, [weak_this]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return false;
            }

            return shared_this->start_impl();
        });
    }

    bool reliable_tcp_server_t::start_impl()
    {
        if (started_)
        {
            return true;
        }
        started_ = true;

        start_accept();
        check_timer_id_ = timer_->start_timer(std::bind(&reliable_tcp_server_t::on_priodically_timer, this), 1, 1);
        return check_timer_id_ != 0;
    }

    void reliable_tcp_server_t::stop()
    {
        if (!started_)
        {
            return;
        }
        started_ = false;
        
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_sync<void>(io_context_, [weak_this]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->stop_impl();
        });
    }

    void reliable_tcp_server_t::stop_impl()
    {
        timer_->stop_timer(check_timer_id_);
        check_timer_id_ = 0;

        do_close();
        sessions_.clear();
        req_2_processor_.clear();
    }

    bool reliable_tcp_server_t::started()
    {
        return started_;
    }

    void reliable_tcp_server_t::register_req_processor(uint32_t cmd, req_processor_t processor)
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_sync<void>(io_context_, [weak_this, cmd, processor]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->register_req_processor_impl(cmd, processor);
        });
    }

    void reliable_tcp_server_t::register_req_processor_impl(uint32_t cmd, req_processor_t processor)
    {
        req_2_processor_[cmd] = processor;
    }

    void reliable_tcp_server_t::unregister_req_processor(uint32_t cmd)
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_sync<void>(io_context_, [weak_this, cmd]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->unregister_req_processor_impl(cmd);
        });
    }

    void reliable_tcp_server_t::unregister_req_processor_impl(uint32_t cmd)
    {
        req_2_processor_.erase(cmd);
    }

    bool reliable_tcp_server_t::send_rsp_for_req(uint32_t session_id, uint32_t cmd, uint32_t seq, uint8_t* rsp_buf, uint32_t rsp_len)
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        return ibase::task::run_task_in_the_iocontext_sync<bool>(io_context_, [weak_this, session_id, cmd, seq, rsp_buf, rsp_len]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return false;
            }

            return shared_this->send_rsp_for_req_impl(session_id, cmd, seq, rsp_buf, rsp_len);
        });
    }

    bool reliable_tcp_server_t::send_rsp_for_req_impl(uint32_t session_id, uint32_t cmd, uint32_t seq, uint8_t* rsp_buf, uint32_t rsp_len)
    {
        return send_packet(session_id, cmd, seq, false, rsp_buf, rsp_len);
    }

    bool reliable_tcp_server_t::publish_notification(uint32_t cmd, uint8_t* notification_buf, uint32_t notification_len)
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        return ibase::task::run_task_in_the_iocontext_sync<bool>(io_context_, [weak_this, cmd, notification_buf, notification_len]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return false;
            }

            return shared_this->publish_notification_impl(cmd, notification_buf, notification_len);
        });
    }

    bool reliable_tcp_server_t::publish_notification_impl(uint32_t cmd, uint8_t* notification_buf, uint32_t notification_len)
    {
        auto seq = ++cur_seq_;
        for (const auto& session : sessions_)
        {
            send_packet(session.second.session_->get_session_id(), cmd, seq, true, notification_buf, notification_len);
        }

        return true;
    }

    bool reliable_tcp_server_t::send_packet(uint32_t session_id, uint32_t cmd, uint32_t seq, bool is_push, uint8_t* rsp_buf, uint32_t rsp_len)
    {
        auto packet = packet_t::build_packet(cmd, seq, is_push, rsp_buf, rsp_len);
        if (packet == nullptr)
        {
            return false;
        }
        
        auto session = get_session(session_id);
        if (!session)
        {
            return false;
        }

        session->send_packet(packet);
        return true;
    }

    void reliable_tcp_server_t::start_accept()
    {
        std::error_code ec;
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
        do_accept();
    }

    void reliable_tcp_server_t::do_close()
    {
        ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("server do_close"));

        if (acceptor_.is_open())
        {
            ibase::logger::write_log(ibase::logger::log_level_debug, "server really do_close");
            asio::error_code ec;
            acceptor_.close(ec);
        }
    }

    void reliable_tcp_server_t::do_accept()
    {
        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        acceptor_.async_accept([weak_this](std::error_code ec, asio::ip::tcp::socket socket)
        {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            if (!shared_this->acceptor_.is_open())
            {
                return;
            }
            
            if (!ec)
            {
                shared_this->add_new_session(std::move(socket));
            }
                
            shared_this->do_accept();
        });
    }

    void reliable_tcp_server_t::dispatch_packet(uint32_t session_id, std::shared_ptr<packet_t> packet)
    {
        on_heartbeat(session_id);

        if (packet->is_push())
        {
            return;
        }

        auto it = req_2_processor_.find(packet->cmd());
        if (it == req_2_processor_.end())
        {
            return;
        }
        
        auto callback = it->second;

        //when we can use move capture of packet, make this async
        callback(session_id, packet);
    }


    void reliable_tcp_server_t::add_new_session(asio::ip::tcp::socket socket)
    {
        auto id = ++cur_session_id;
        auto timetamp = std::chrono::steady_clock::now();

        std::weak_ptr<reliable_tcp_server_t> weak_this(shared_from_this());
        auto session = std::make_shared<reliable_tcp_session_t>(id, std::move(socket), io_context_, [weak_this](uint32_t session_id, std::shared_ptr<packet_t> packet) {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }
            shared_this->dispatch_packet(session_id, packet);
        });

        if (!session)
        {
            return;
        }

        session->start();
        sessions_[id] = {session, timetamp};
    }

    std::shared_ptr<reliable_tcp_session_t> reliable_tcp_server_t::get_session(uint32_t session_id)
    {
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            return nullptr;
        }
        
        return it->second.session_;
    }

    void reliable_tcp_server_t::on_heartbeat(uint32_t session_id)
    {
        auto it = sessions_.find(session_id);
        if (it == sessions_.end())
        {
            return;
        }
        
        it->second.last_recv_timepoint_ = std::chrono::steady_clock::now();
    }

    void reliable_tcp_server_t::on_priodically_timer()
    {
        auto cur_timepoint = std::chrono::steady_clock::now();
        
        for (auto it = sessions_.begin(); it != sessions_.end(); )
        {
            auto time_passed_by_seconds = std::chrono::duration_cast<std::chrono::seconds>(cur_timepoint - it->second.last_recv_timepoint_);
            if (time_passed_by_seconds.count() < max_heartbeat_interval_seconds)
            {
                ++it;
                continue;
            }
            
            ibase::logger::write_log(ibase::logger::log_level_debug, fmt::format("session not heartbeat, remove it =  {}", it->first));
            
            it = sessions_.erase(it);
        }
    }

}
