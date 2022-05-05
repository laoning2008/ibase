#include "ithread.hpp"
namespace ibase {

    ithread::ithread() : work_guard_(io_context_.get_executor())
    {
        t_ = std::thread([this]()
        {
            io_context_.run();
        });
    }

    ithread::~ithread()
    {
        stop();
    }

    void ithread::stop()
    {
        io_context_.stop();
        if (t_.joinable())
        {
            t_.join();
        }
    }

    std::thread::id ithread::get_id() const noexcept
    {
        return t_.get_id();
    }

    asio::io_context& ithread::get_io_context() noexcept
    {
        return io_context_;
    }

}
