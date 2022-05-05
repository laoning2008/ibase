#pragma once
#include <string>
#include <functional>


namespace ibase
{
    namespace logger
    {
        enum log_level_t
        {
            log_level_debug,
            log_level_info,
            log_level_warn,
            log_level_error,
        };

        using log_callback_t = std::function<void(log_level_t level, const std::string& msg)>;
    
        void set_logger_callback(log_callback_t log_callback);
        void write_log(log_level_t level, const std::string& msg);
    }
}
