#include "recently_packet_tracker.hpp"
#include <chrono>

namespace ibase
{
    bool recently_packet_tracker_t::on_receive_packet(const uint32_t cmd, const uint32_t seq)
    {
        uint64_t id = packet_id(cmd, seq);
        
		auto cur_tick = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

		// 清除60秒以前收到的命令
		auto offset = cur_tick - first_tick_;
		if (offset > max_packet_life_time_in_seconds - 1)
		{
			auto span = offset + 1 - max_packet_life_time_in_seconds;

			for (auto i = 0; i < max_packet_life_time_in_seconds && i < span; ++i)
			{
				auto& packet_id_array = packet_time_index_array_[(first_tick_ + i) % max_packet_life_time_in_seconds];
				for (auto& id : packet_id_array)
				{
					recently_packet_ids_.erase(id);
				}
				packet_id_array.clear();
			}

			first_index_ = (first_index_ + span) % max_packet_life_time_in_seconds;
			offset = max_packet_life_time_in_seconds - 1;
			first_tick_ = cur_tick - offset;
		}

		// 收到过这个命令序列。不记录，返回TRUE.
		if (recently_packet_ids_.find(id) != recently_packet_ids_.end())
		{
			return true;
		}

		packet_time_index_array_[(first_index_ + offset) % max_packet_life_time_in_seconds].push_back(id);
		recently_packet_ids_.insert(id);
		return false;
    }

    void recently_packet_tracker_t::clear()
    {
        recently_packet_ids_.clear();
        for (auto& item : packet_time_index_array_)
        {
            item.clear();
        }
    }

    uint64_t recently_packet_tracker_t::packet_id(const uint32_t cmd, const uint32_t seq)
    {
        uint64_t id = cmd;
        id = (id << 32)|seq;
        return id;
    }
}
