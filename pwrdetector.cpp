#include <vector>
#include <unordered_map>
#include <algorithm>
#include "detector.hpp"
#include "vectorclock.hpp"

class PWRDetector : public Detector {
    private:
        typedef int Epoch;
        struct EpochVCPair {
            Epoch epoch;
            VectorClock vector_clock;
            ThreadID thread_id;
        };
        struct EpochLSPair {
            Epoch epoch;
            std::vector<ResourceName> lockset;
            ThreadID thread_id;
        };

        std::unordered_map<ThreadID, std::vector<ResourceName>> lockset = {};
        std::unordered_map<ResourceName, std::vector<EpochVCPair>> critical_sections = {};
        std::unordered_map<ThreadID, VectorClock> vector_clocks = {};
        std::unordered_map<ResourceName, Epoch> last_aquires = {};
        std::unordered_map<ResourceName, VectorClock> last_writes = {};
        std::unordered_map<ResourceName, std::vector<EpochLSPair>> read_write_events = {};

        VectorClock get_or_create_vector_clock(ThreadID thread_id, bool increment_thread_on_create) {
            auto vector_clock = vector_clocks.find(thread_id);
            if(vector_clock == vector_clocks.end()) {
                VectorClock new_vector_clock;
                if(increment_thread_on_create) {
                    new_vector_clock.increment(thread_id);
                }
                vector_clocks[thread_id] = new_vector_clock;
                return new_vector_clock;
            } else {
                return vector_clock->second;
            }
        }

        std::vector<ResourceName> get_or_create_lockset(ThreadID thread_id) {
            auto locks = lockset.find(thread_id);

            if(locks == lockset.end()) {
                std::vector<ResourceName> new_lockset;
                lockset[thread_id] = new_lockset;
                return new_lockset;
            } else {
                return locks->second;
            }
        }

        std::vector<EpochLSPair> get_or_create_read_write_event(ResourceName resource_name) {
            auto rw = read_write_events.find(resource_name);

            if(rw == read_write_events.end()) {
                std::vector<EpochLSPair> new_rw;
                read_write_events[resource_name] = new_rw;
                return new_rw;
            } else {
                return rw->second;
            }
        }

        void pwr_algorithm(ThreadID thread_id) {
            auto locks = lockset.find(thread_id);
            if(locks == lockset.end()) {
                return;
            }

            for(ResourceName lock : locks->second) {
                auto critical_section = critical_sections.find(lock);
                if(critical_section == critical_sections.end()) {
                    continue;
                }

                for(EpochVCPair epoch_vc_pair : critical_section->second) {
                    auto vector_clock = vector_clocks.find(thread_id);
                    if(vector_clock == vector_clocks.end()) {
                        continue;
                    }

                    Epoch* epoch = vector_clock->second.find(epoch_vc_pair.thread_id);
                    if(epoch == NULL) {
                        continue;
                    }

                    if(epoch_vc_pair.epoch < *epoch) {
                        vector_clocks[thread_id] = vector_clock->second.merge(epoch_vc_pair.vector_clock);
                    }
                }
            }
        }

        void report_potential_race(ResourceName resource_name, TracePosition trace_position, ThreadID thread_id_1, ThreadID thread_id_2) {
            if(this->lockframe != NULL) {
                this->lockframe->report_race(DataRace{ resource_name, trace_position, thread_id_1, thread_id_2 });
            }
        }
    public:
        void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            VectorClock current_vector_clock = get_or_create_vector_clock(thread_id, true);

            // Th(i) = Th(i) |_| L_w(x)
            // "Compare values per thread, set to max ==> merge operation"
            auto last_write_vc_ptr = last_writes.find(resource_name);
            if(last_write_vc_ptr != last_writes.end()) {
                vector_clocks[thread_id] = current_vector_clock.merge(last_write_vc_ptr->second);
            }

            pwr_algorithm(thread_id);

            Epoch current_epoch = current_vector_clock.find_or(thread_id, 0);
            std::vector<ResourceName> current_lockset = get_or_create_lockset(thread_id);


            // (i#Th(i)[i], LS_t(i))
            // Current "timestamp" of the calling thread with its current locks.
            EpochLSPair current_epoch_ls_pair = { current_epoch, current_lockset, thread_id };

            // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
            // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
            std::vector<EpochLSPair> read_write_events_new = { current_epoch_ls_pair };
            for(EpochLSPair rw_pair : get_or_create_read_write_event(resource_name)) {
                if(rw_pair.epoch > current_vector_clock.find_or(rw_pair.thread_id, 0)) {
                    read_write_events_new.push_back(rw_pair);
                }
            }
            read_write_events[resource_name] = read_write_events_new;

            // Race check
            // Go through each currently stored read_write_event,
            // check if any lockset doesn't overlap with current one and
            // the other epoch is larger than thread_id's currently stored one for the other thread.
            for(EpochLSPair rw_pair : read_write_events_new) {
                if(rw_pair.epoch > current_vector_clock.find_or(rw_pair.thread_id, 0)) {
                    for(ResourceName rn_t1 : current_lockset) {
                        for(ResourceName rn_t2 : rw_pair.lockset) {
                            if(rn_t1 == rn_t2) {
                                report_potential_race(resource_name, trace_position, thread_id, rw_pair.thread_id);
                            }
                        }
                    }
                }
            }

            current_vector_clock.increment(thread_id);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            pwr_algorithm(thread_id);

            VectorClock current_vector_clock = get_or_create_vector_clock(thread_id, true);
            Epoch current_epoch = current_vector_clock.find_or(thread_id, 0);
            std::vector<ResourceName> current_lockset = get_or_create_lockset(thread_id);

            // (i#Th(i)[i], LS_t(i))
            // Current "timestamp" of the calling thread with its current locks.
            EpochLSPair current_epoch_ls_pair = { current_epoch, current_lockset, thread_id };
            
            // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
            // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
            std::vector<EpochLSPair> read_write_events_new = {current_epoch_ls_pair};
            for(EpochLSPair rw_pair : get_or_create_read_write_event(resource_name)) {
                if(rw_pair.epoch > current_vector_clock.find_or(rw_pair.thread_id, 0)) {
                    read_write_events_new.push_back(rw_pair);
                }
            }
            read_write_events[resource_name] = read_write_events_new;

            // Race check
            // Go through each currently stored read_write_event,
            // check if any lockset doesn't overlap with current one and
            // the other epoch is larger than thread_id's currently stored one for the other thread.
            for(EpochLSPair rw_pair : read_write_events_new) {
                if(rw_pair.epoch > current_vector_clock.find_or(rw_pair.thread_id, 0)) {
                    bool overlap = false;
                    for(ResourceName rn_t1 : current_lockset) {
                        for(ResourceName rn_t2 : rw_pair.lockset) {
                            if(rn_t1 == rn_t2) {
                                overlap = true;
                            }
                        }
                    }
                    if(!overlap) {
                        report_potential_race(resource_name, trace_position, thread_id, rw_pair.thread_id);
                    }
                }
            }

            last_writes[resource_name] = current_vector_clock;

            current_vector_clock.increment(thread_id);
        }

        void aquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            pwr_algorithm(thread_id);

            // Add Resource to Lockset
            auto current_lockset_ptr = lockset.find(thread_id);
            auto current_lockset = (current_lockset_ptr == lockset.end()) ?  std::vector<ResourceName>() : current_lockset_ptr->second;
            if(std::find(current_lockset.begin(), current_lockset.end(), resource_name) != current_lockset.end()) {
                current_lockset.push_back(resource_name);
            }
            lockset[thread_id] = current_lockset;

            // Set Aquire History
            auto current_vector_clock_ptr = vector_clocks.find(thread_id);
            if(current_vector_clock_ptr == vector_clocks.end()) {
                last_aquires[resource_name] = 0;
                vector_clocks[thread_id] = VectorClock();
            } else {
                Epoch* epoch = current_vector_clock_ptr->second.find(thread_id);
                if(epoch == NULL) {
                    last_aquires[resource_name] = 0;
                } else {
                    last_aquires[resource_name] = *epoch;

                    // Increment only necessary if epoch > 0
                    vector_clocks.find(thread_id)->second.increment(thread_id);
                }
            }
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            pwr_algorithm(thread_id);

            // Remove Resource from Lockset
            auto current_lockset_ptr = lockset.find(thread_id);
            if(current_lockset_ptr != lockset.end()) {
                auto resource_iter = std::find(current_lockset_ptr->second.begin(), current_lockset_ptr->second.end(), resource_name);
                if(resource_iter != current_lockset_ptr->second.end()) {
                    current_lockset_ptr->second.erase(resource_iter);
                }
            }

            // Add to critical section
            auto current_critical_section_ptr = critical_sections.find(resource_name);
            auto current_critical_section = (current_critical_section_ptr != critical_sections.end()) ? current_critical_section_ptr->second : std::vector<EpochVCPair>();
            auto last_aquire_ptr = last_aquires.find(resource_name);
            auto vector_clock_ptr = vector_clocks.find(thread_id);
            current_critical_section.push_back(EpochVCPair{
                (last_aquire_ptr != NULL) ? last_aquire_ptr->second : 0,
                (vector_clock_ptr != NULL) ? vector_clock_ptr->second : VectorClock(),
                thread_id
            });

            if(vector_clock_ptr == NULL) {
                vector_clocks[thread_id] = VectorClock();
            } else {
                vector_clocks.find(thread_id)->second.increment(thread_id);
            }
        }

        void fork_event(ThreadID, TracePosition, ResourceName) {

        }

        void join_event(ThreadID, TracePosition, ResourceName) {

        }
};