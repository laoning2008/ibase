#include "ilogger.hpp"
#include <mutex>


namespace ibase
{
    namespace logger
    {
        std::mutex lock_;
        log_callback_t log_callback_;

        void set_logger_callback(log_callback_t log_callback)
        {
            std::lock_guard<std::mutex> auto_lock(lock_);
            log_callback_ = log_callback;
        }
    
        void write_log(log_level_t level, const std::string& msg)
        {
            std::lock_guard<std::mutex> auto_lock(lock_);
            if (log_callback_)
            {
                log_callback_(level, msg);
            }
        }
    }
}
