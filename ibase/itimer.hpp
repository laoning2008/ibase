#pragma once
#include <functional>
#include <map>
#include <memory>
#include <atomic>
#include <asio.hpp>

namespace ibase
{
    class itimer : public std::enable_shared_from_this<itimer>
    {
        struct timer_info_t
        {
            std::shared_ptr<asio::steady_timer> timer_;
            std::function<void()> task_;
            uint32_t delay_seconds_;
            uint32_t interval_seconds_;
        };
        
        using timer_map_t = std::map<uint32_t, timer_info_t>;

    public:
        itimer(asio::io_context& io_context);
        ~itimer();
    public:
        uint32_t start_timer(std::function<void()> task, uint32_t delay_seconds, uint32_t interval_seconds);
        void stop_timer(uint32_t timer_id);
    private:
        void start_timer_impl(uint32_t timer_id, std::function<void()> task, uint32_t delay_seconds, uint32_t interval_seconds);
        void stop_timer_impl(uint32_t timer_id);
        void async_wait_after(uint32_t timer_id, uint32_t seconds);
    private:
        itimer(const itimer& other) = delete;
        itimer(itimer&& other) = delete;
        itimer& operator=(const itimer& other) = delete;
        itimer& operator=(itimer&& other) = delete;

    private:
        void on_timer(uint32_t timer_id);
        void reschedule_timer(uint32_t timer_id);
    private:
        asio::io_context&                                           io_context_;
        std::atomic<uint32_t>                                       cur_timer_id_{0};
        timer_map_t                                                 timers_;
    };
}
