#pragma once
#include <set>
#include <vector>

namespace ibase
{
    //not thread safe
    class recently_packet_tracker_t
    {
        static constexpr uint32_t max_packet_life_time_in_seconds = 60;
        using packet_id_set_t = std::set<uint64_t>;
        using packet_id_vec = std::vector<uint64_t>;        
    public:
        bool on_receive_packet(const uint32_t cmd, const uint32_t seq);
        void clear();
    private:
        uint64_t packet_id(const uint32_t cmd, const uint32_t seq);
    private:
        packet_id_set_t              recently_packet_ids_;
        packet_id_vec                packet_time_index_array_[max_packet_life_time_in_seconds];
        uint64_t                     first_tick_{0};
        uint32_t                     first_index_{0};
    };
}
