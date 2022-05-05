#include "itimer.hpp"
#include "task_runner.hpp"

namespace ibase {

    itimer::itimer(asio::io_context& io_context)
    : io_context_(io_context)
    {

    }

    itimer::~itimer()
    {

    }

    uint32_t itimer::start_timer(std::function<void()> task, uint32_t delay_seconds, uint32_t interval_seconds)
    {
        auto timer_id = ++cur_timer_id_;
        std::weak_ptr<itimer> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, timer_id, task, delay_seconds, interval_seconds]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->start_timer_impl(timer_id, task, delay_seconds, interval_seconds);
        });

        return timer_id;
    }

    void itimer::stop_timer(uint32_t timer_id)
    {
        std::weak_ptr<itimer> weak_this(shared_from_this());
        ibase::task::run_task_in_the_iocontext_async(io_context_, [weak_this, timer_id]() {
            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }

            shared_this->stop_timer_impl(timer_id);
        });
    }

    void itimer::start_timer_impl(uint32_t timer_id, std::function<void()> task, uint32_t delay_seconds, uint32_t interval_seconds)
    {
        timer_info_t timer_info{ std::make_shared<asio::steady_timer>(io_context_), task, delay_seconds, interval_seconds };
        timers_[timer_id] = timer_info;
        
        async_wait_after(timer_id, interval_seconds);
    }

    void itimer::stop_timer_impl(uint32_t timer_id)
    {
        auto it = timers_.find(timer_id);

        if (it == timers_.end())
        {
            return;
        }

        std::error_code rc;
        it->second.timer_->cancel(rc);

        timers_.erase(it);
    }

    void itimer::on_timer(uint32_t timer_id)
    {
        auto it = timers_.find(timer_id);

        if (it == timers_.end())
        {
            return;
        }

        //callback
        it->second.task_();
        reschedule_timer(it->first);
    }

    void itimer::reschedule_timer(uint32_t timer_id)
    {
        auto it = timers_.find(timer_id);

        if (it == timers_.end())
        {
            return;
        }

        //remove or reschedule
        if (it->second.interval_seconds_ == 0)
        {
            timers_.erase(it);
        }
        else
        {
            async_wait_after(timer_id, it->second.interval_seconds_);
        }
    }

    void itimer::async_wait_after(uint32_t timer_id, uint32_t seconds)
    {
        auto it = timers_.find(timer_id);
        if (it == timers_.end())
        {
            return;
        }

        it->second.timer_->expires_after(std::chrono::seconds(it->second.interval_seconds_));

        std::weak_ptr<itimer> weak_this(shared_from_this());
        it->second.timer_->async_wait([weak_this, timer_id](const asio::error_code& ec) {
            if (ec)
            {
                return;
            }

            auto shared_this = weak_this.lock();
            if (!shared_this)
            {
                return;
            }
            shared_this->on_timer(timer_id);
        });
    }
}
