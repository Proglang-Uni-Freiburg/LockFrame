#include <vector>
#include <unordered_map>
#include <algorithm>
#include "detector.hpp"
#include "vectorclock.hpp"

class PWRDetector : public Detector {
    private:
        struct EpochVCPair {
            Epoch epoch;
            VectorClock vector_clock;
        };
        struct EpochLSPair {
            Epoch epoch;
            std::vector<ResourceName> lockset;
        };

        // LS(i)
        std::unordered_map<ThreadID, std::vector<ResourceName>> lockset = {};
        // H(y)
        std::unordered_map<ResourceName, std::vector<EpochVCPair>> history = {};
        // Th(i)
        std::unordered_map<ThreadID, VectorClock> vector_clocks = {};
        // RW(x)
        std::unordered_map<ResourceName, std::vector<EpochLSPair>> read_write_events = {};
        // Acq(y)
        std::unordered_map<ResourceName, Epoch> last_aquires = {};
        // L_w(x)
        std::unordered_map<ResourceName, VectorClock> last_writes_vc = {};
        // L_wt(x)
        std::unordered_map<ResourceName, ThreadID> last_writes_thread = {};
        // L_wl(x)
        std::unordered_map<ResourceName, std::vector<ResourceName>> last_writes_ls = {};

        /**
         * @brief Prints all current global variable values
         * 
         * @param text 
         * @param thread_id 
         * @param resource_name 
         */
        void debug_print(std::string text, ThreadID thread_id, ResourceName resource_name) {
            VectorClock current_vector_clock = vector_clocks.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple(thread_id)).first->second;
            std::vector<ResourceName> current_lockset = lockset.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;
            std::vector<EpochLSPair> current_read_write_events = read_write_events.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            std::vector<EpochVCPair> current_history = history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            auto last_write_vc_ptr = last_writes_vc.find(resource_name);
            auto last_write_ls_ptr = last_writes_ls.find(resource_name);
            auto last_aquire = last_aquires.find(resource_name);

            printf("\n---DEBUG [%s] T<%d> R<%s>---\n", text.c_str(), thread_id, resource_name.c_str());
            
            printf("vc: { ");
            for(auto epoch : current_vector_clock.find_all()) {
                printf("T%d:%d ", epoch.thread_id, epoch.value);
            }
            printf("}\n");

            printf("lockset: { ");
            for(auto res : current_lockset) {
                printf("%s", res.c_str());
            }
            printf(" }\n");

            printf("history: { ");
            for(auto pair : current_history) {
                printf("%d#%d, { ", pair.epoch.thread_id, pair.epoch.value);
                for(auto epoch : pair.vector_clock.find_all()) {
                    printf("%d@%d ", epoch.thread_id, epoch.value);
                }
                printf("}");
            }
            printf(" }\n");

            printf("rw: { ");
            for(auto pairs : current_read_write_events) {
                printf("%d#%d { ", pairs.epoch.thread_id, pairs.epoch.value);
                for(auto res : pairs.lockset) {
                    printf("%s", res.c_str());
                }
                printf(" } ");
            }
            printf("}\n");

            printf("acq: ");
            if(last_aquire != last_aquires.end()) {
                printf("%d#%d", last_aquire->second.thread_id, last_aquire->second.value);
            }
            printf("\n");

            printf("l_w: { ");
            if(last_write_vc_ptr != last_writes_vc.end()) {
                for(auto epoch : last_write_vc_ptr->second.find_all()) {
                    printf("T%d:%d ", epoch.thread_id, epoch.value);
                }
            }
            printf(" }\n");

            printf("l_wt: %d\n", (last_writes_thread.find(resource_name) != last_writes_thread.end()) ? last_writes_thread.find(resource_name)->second : 0);

            printf("l_wls: { ");
            if(last_write_ls_ptr != last_writes_ls.end()) {
                for(auto res : last_write_ls_ptr->second) {
                    printf("%s ", res.c_str());
                }
            }
            printf(" }\n");

            printf("------\n");
        }

        // w3
        void pwr_history_sync(ThreadID thread_id) {
            std::vector<ResourceName> current_lockset = lockset.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;

            for(ResourceName lock : current_lockset) {
                std::vector<EpochVCPair> lock_history = history.emplace(std::piecewise_construct, std::forward_as_tuple(lock), std::forward_as_tuple()).first->second;

                for(EpochVCPair epoch_vc_pair : lock_history) {
                    VectorClock vector_clock = vector_clocks.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;

                    if(epoch_vc_pair.epoch.value < vector_clock.find(epoch_vc_pair.epoch.thread_id)) {
                        vector_clocks[thread_id] = vector_clock.merge(epoch_vc_pair.vector_clock);
                    }
                }
            }
        }

        // RW = { (i#Th(i)[i], LS_t(i) } U { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
        std::vector<EpochLSPair> update_read_write_events(ThreadID thread_id, ResourceName resource_name, std::vector<EpochLSPair> rw_pairs, VectorClock vc, std::vector<ResourceName> ls) {
            // (i#Th(i)[i], LS_t(i))
            // Current "timestamp" of the calling thread with its current locks.
            std::vector<EpochLSPair> read_write_events_new = {
                EpochLSPair { Epoch { thread_id, vc.find(thread_id) }, ls }
            };

            // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
            // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
            for(EpochLSPair rw_pair : rw_pairs) {
                if(rw_pair.epoch.value > vc.find(rw_pair.epoch.thread_id)) {
                    read_write_events_new.push_back(rw_pair);
                }
            }

            read_write_events[resource_name] = read_write_events_new;
            return read_write_events_new;
        }

        bool check_locksets_overlap(std::vector<ResourceName> ls1, std::vector<ResourceName> ls2) {
            for(ResourceName rn_t1 : ls1) {
                for(ResourceName rn_t2 : ls2) {
                    if(rn_t1 == rn_t2) {
                        return true;
                    }
                }
            }

            return false;
        }

        void add_races(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name, std::vector<EpochLSPair> rw_pairs, VectorClock vc, std::vector<ResourceName> ls) {
            // Race check
            // Go through each currently stored read_write_event,
            // check if any lockset doesn't overlap with current one and
            // the other epoch is larger than thread_id's currently stored one for the other thread.
            for(EpochLSPair rw_pair : rw_pairs) {
                if(rw_pair.epoch.value > vc.find(rw_pair.epoch.thread_id)) {
                    if(!check_locksets_overlap(ls, rw_pair.lockset)) {
                        report_potential_race(resource_name, trace_position, thread_id, rw_pair.epoch.thread_id);
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
            VectorClock current_vector_clock = vector_clocks.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple(thread_id)).first->second;
            VectorClockValue current_vc_value = current_vector_clock.find(thread_id);
            std::vector<ResourceName> current_lockset = lockset.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;
            std::vector<EpochLSPair> current_read_write_events = read_write_events.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;

            auto last_write_thread_ptr = last_writes_thread.find(resource_name);
            if(last_write_thread_ptr != last_writes_thread.end()) {
                ThreadID last_write_thread = last_write_thread_ptr->second;
                std::vector<ResourceName> last_write_ls = last_writes_ls.find(resource_name)->second;
                VectorClock last_write_vc = last_writes_vc.find(resource_name)->second;

                // if L_w(x)[j] > Th(i)[j] AND L_wl(x) ∩ LS(i) = {}
                // THERE IS A DISCREPANCY IN THE PAPER VS DISSERTATION HERE!
                // L_w and Th are swapped but then they wouldn't find the WR-Race
                if(last_write_vc.find(last_write_thread) > current_vector_clock.find(last_write_thread) && !check_locksets_overlap(last_write_ls, current_lockset)) {
                    add_races(
                        thread_id,
                        trace_position,
                        resource_name,
                        current_read_write_events,
                        current_vector_clock,
                        current_lockset
                    );
                }
            }

            // Th(i) = Th(i) |_| L_w(x)
            // "Compare values per thread, set to max ==> merge operation"
            auto last_write_vc_ptr = last_writes_vc.find(resource_name);
            if(last_write_vc_ptr != last_writes_vc.end()) {
                vector_clocks[thread_id] = current_vector_clock = current_vector_clock.merge(last_write_vc_ptr->second);
            }

            pwr_history_sync(thread_id);
            current_vector_clock = vector_clocks.find(thread_id)->second;

            std::vector<EpochLSPair> current_rw_pairs = read_write_events.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;

            add_races(
                thread_id,
                trace_position,
                resource_name,
                current_rw_pairs,
                current_vector_clock,
                current_lockset
            );

            auto read_write_events_new = update_read_write_events(
                thread_id,
                resource_name,
                current_rw_pairs,
                current_vector_clock,
                current_lockset);

            current_vector_clock.increment(thread_id);
            vector_clocks[thread_id] = current_vector_clock;

            debug_print("read", thread_id, resource_name);
        }

        void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            pwr_history_sync(thread_id);

            VectorClock current_vector_clock = vector_clocks.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple(thread_id)).first->second;
            std::vector<ResourceName> current_lockset = lockset.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;
            std::vector<EpochLSPair> current_rw_pairs = read_write_events.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;

            add_races(
                thread_id,
                trace_position,
                resource_name,
                current_rw_pairs,
                current_vector_clock,
                current_lockset
            );

            auto read_write_events_new = update_read_write_events(
                thread_id,
                resource_name,
                current_rw_pairs,
                current_vector_clock,
                current_lockset);

            last_writes_vc[resource_name] = current_vector_clock;
            last_writes_thread[resource_name] = thread_id;
            last_writes_ls[resource_name] = current_lockset;

            current_vector_clock.increment(thread_id);
            vector_clocks[thread_id] = current_vector_clock;

            debug_print("write", thread_id, resource_name);
        }

        void aquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            std::vector<ResourceName> current_lockset = lockset.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple()).first->second;
            VectorClock current_vector_clock = vector_clocks.emplace(std::piecewise_construct, std::forward_as_tuple(thread_id), std::forward_as_tuple(thread_id)).first->second;
            
            pwr_history_sync(thread_id);

            // Add Resource to Lockset
            if(std::find(current_lockset.begin(), current_lockset.end(), resource_name) == current_lockset.end()) {
                current_lockset.push_back(resource_name);
                lockset[thread_id] = current_lockset;
            }

            // Set Aquire History
            last_aquires[resource_name] = Epoch { thread_id, current_vector_clock.find(thread_id) };
            current_vector_clock.increment(thread_id);
            vector_clocks[thread_id] = current_vector_clock;

            debug_print("aquire", thread_id, resource_name);
        }

        void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
            pwr_history_sync(thread_id);

            // Remove Resource from Lockset
            auto current_lockset_ptr = lockset.find(thread_id);
            if(current_lockset_ptr != lockset.end()) {
                auto resource_iter = std::find(current_lockset_ptr->second.begin(), current_lockset_ptr->second.end(), resource_name);
                if(resource_iter != current_lockset_ptr->second.end()) {
                    current_lockset_ptr->second.erase(resource_iter);
                }
            }

            // Add to history
            std::vector<EpochVCPair> current_history = history.emplace(std::piecewise_construct, std::forward_as_tuple(resource_name), std::forward_as_tuple()).first->second;
            auto last_aquire_ptr = last_aquires.find(resource_name);
            auto vector_clock_ptr = vector_clocks.find(thread_id);
            current_history.push_back(EpochVCPair{
                last_aquire_ptr->second,
                (vector_clock_ptr != NULL) ? vector_clock_ptr->second : VectorClock(thread_id)
            });
            history[resource_name] = current_history;

            if(vector_clock_ptr == NULL) {
                vector_clocks[thread_id] = VectorClock(thread_id);
            } else {
                vector_clocks.find(thread_id)->second.increment(thread_id);
            }

            debug_print("release", thread_id, resource_name);
        }

        void fork_event(ThreadID, TracePosition, ResourceName) {

        }

        void join_event(ThreadID, TracePosition, ResourceName) {

        }
};