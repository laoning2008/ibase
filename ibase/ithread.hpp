#include <thread>
#include <asio.hpp>

namespace ibase
{
    class ithread final
    {
    public:
        ithread();
        ~ithread();
        
        void stop();
        std::thread::id get_id() const noexcept;
        asio::io_context& get_io_context() noexcept;
        
    private:
        std::thread t_;
        asio::io_context io_context_;
        asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    };
}
