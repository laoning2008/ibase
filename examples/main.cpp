#include <iostream>
#include <asio.hpp>
#include <chrono>
#include "ithread.hpp"
#include "itimer.hpp"
#include "task_runner.hpp"
#include "reliable_tcp_client.hpp"
#include "reliable_tcp_server.hpp"
#include "ilogger.hpp"
#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

char *req_buf = "hello from client";
uint32_t req_len = strlen(req_buf);
char *rsp_buf = "world from server";
uint32_t rsp_len = strlen(rsp_buf);
char *push_buf = "push from server";
uint32_t push_len = strlen(push_buf);

int main(int argc, char** argv)
{
    ibase::ithread worker_thread_;
    ibase::ithread test_thread_;
    
    auto server_ = std::make_shared<ibase::reliable_tcp_server_t>(worker_thread_.get_io_context(), 8090);
    
    spdlog::set_pattern("[%Y-%m-%d-%H:%M:%S.%e] [Process %P] [thread %t] %v");
    ibase::logger::set_logger_callback([](ibase::logger::log_level_t level, const std::string& msg) {
        spdlog::info(msg);
    });
    
    if (!server_->start())
    {
        ibase::logger::write_log(ibase::logger::log_level_info, "server start failed");
        return -1;
    }
    server_->register_req_processor(1, [&server_](uint32_t session_id, std::shared_ptr<ibase::packet_t> packet) {
        std::string msg((char*)packet->body(), packet->body_length());
        ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("server_callback----recv req from client, cmd = {}, seq = {}, msg = {}", packet->cmd(), packet->seq(), msg));
        
        ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("server_callback----send rsp to client, cmd = {}, seq = {}, msg = {}", packet->cmd(), packet->seq(), rsp_buf));
        server_->send_rsp_for_req(session_id, packet->cmd(), packet->seq(), (uint8_t*)rsp_buf, rsp_len);
    });
    

    //test
    auto test_timer_ = std::make_shared<ibase::itimer>(test_thread_.get_io_context());

    std::vector<std::shared_ptr<ibase::reliable_tcp_client_t>> vec_client;
    for (int i = 0; i < 100; ++i)
    {
        auto client_ = std::make_shared<ibase::reliable_tcp_client_t>(worker_thread_.get_io_context());
        ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("create client = {}", reinterpret_cast<uint64_t>(client_.get())));

        if (!client_->start("localhost", 8090))
        {
            ibase::logger::write_log(ibase::logger::log_level_info, "client start failed");
            return -1;
        }
        client_->subscribe_notification(2, [](std::shared_ptr<ibase::packet_t> packet) {
            std::string msg((char*)packet->body(), packet->body_length());
            ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("client_callback----recv push from server, cmd = {}, seq = {}, msg = {}", packet->cmd(), packet->seq(), msg));
        });


        auto client_test_timer_id = test_timer_->start_timer([client_]() {
            ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("client req, client = {}", reinterpret_cast<uint64_t>(client_.get())));

            client_->send_req_async(1, (uint8_t*)req_buf, req_len, nullptr, [](uint32_t send_id, int result, std::shared_ptr<ibase::packet_t> packet) {
                if (!packet)
                {
                    ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("req rsp null packet"));
                    return;
                }

                if (result != 0)
                {
                    ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("client_callback----req timeout or failed, cmd = {}, seq = {}", packet->cmd(), packet->seq()));
                    return;
                }

                std::string msg((char*)packet->body(), packet->body_length());
                ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("client_callback----recv rsp from server, cmd = {}, seq = {}, msg = {}", packet->cmd(), packet->seq(), msg));
            });
        }, 1, 1);

        vec_client.push_back(client_);
    }
   
    
    auto server_test_timer_id = test_timer_->start_timer([&server_]() {
        ibase::logger::write_log(ibase::logger::log_level_info, fmt::format("server_publish----push notification to client, cmd = {}, msg = {}", 2, push_buf));
        server_->publish_notification(2, (uint8_t*)push_buf, push_len);
    }, 1, 1);
    
    
    asio::io_context io_context_;
    
    asio::signal_set signals(io_context_, SIGINT, SIGTERM);
    signals.async_wait([&worker_thread_,  &server_, &test_thread_, &io_context_](const std::error_code& error, int signal_number) {
        //client_->stop();
        //server_->stop();
        io_context_.stop();
    });
    
    
    io_context_.run();
    
    test_thread_.stop();
    worker_thread_.stop();
    
    std::cout << "exit main now" << std::endl;
    
    return 0;
}
