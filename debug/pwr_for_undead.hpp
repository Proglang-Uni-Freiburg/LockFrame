/**
 * Implements the PWR algorithm found at https://github.com/sulzmann/source/blob/main/mplr2020-tr.pdf
 * Adds release information to history
 * 
 * Optimizations:
 *      - Paper
 *      - Use pointers
 *      - HistAddNoReads
 *      - Write-NoSync
 *      - Fork-NoSync
 *      - Read-NoSync
 *      - Shared Pointer
 *      - LocalHistRemove if V'[j] <= V[j]
 *      - LocalHistRemove
 */

#ifndef PWR_FOR_UNDEAD_DETECTOR_H
#define PWR_FOR_UNDEAD_DETECTOR_H

#include <vector>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include "../detector.hpp"
#include "../vectorclock.hpp"

#define THREAD_HISTORY_SIZE 5

class LockFrame;
class PWRForUndeadDetector : public Detector {
    private:
        struct EpochVCPair {
            Epoch epoch;
            VectorClock acq_vc;
            VectorClock rel_vc;
        };
        struct EpochLSPair {
            Epoch epoch;
            std::vector<ResourceName> lockset;
            bool is_write;
        };
        struct Thread {
            ThreadID id;
            // LS(i)
            std::vector<ResourceName> lockset = {};
            // H(y)
            std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>> history = {};
            std::unordered_map<ResourceName, std::shared_ptr<EpochVCPair>> history_last_read_only = {};
            // Th(i)
            VectorClock vector_clock = {};
            std::unordered_map<ResourceName, TracePosition> last_read_merges = {};
            std::unordered_map<ResourceName, TracePosition> lock_acquired_at = {};
            TracePosition last_write_at = 0;
        };
        struct Resource {
            // RW(x)
            std::vector<EpochLSPair> read_write_events = {};
            // Acq(y)
            Epoch last_acquire = {};
            VectorClock last_acquire_vc = {};
            // L_w(x)
            VectorClock last_write_vc = {};
            // L_wt(x)
            ThreadID last_write_thread = {};
            // L_wl(x)
            std::vector<ResourceName> last_write_ls = {};
            // Helper variable, we need to check if write already occured,
            // but defaults stored in last_* variables don't tell us.
            bool last_write_occured = false;
            TracePosition last_write_occured_at = 0;
        };

        std::unordered_map<ThreadID, Thread> threads = {};
        std::unordered_map<ResourceName, Resource> resources = {};
        std::unordered_map<ResourceName, VectorClock> notifies = {};
        // We don't know about all threads at the beginning, so we have to save a global history in order to load it into a newly spawned thread.
        // This history is still optimized for limited size.
        std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>> global_history = {};

        void pwr_history_sync(Thread* thread, Resource* resource);
        void update_read_write_events(Thread* thread, Resource* resource, bool is_write);
        bool check_locksets_overlap(std::vector<ResourceName> *ls1, std::vector<ResourceName> *ls2);
        void add_races(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name, std::vector<EpochLSPair> *rw_pairs, VectorClock *vc, std::vector<ResourceName> *ls);
        void report_potential_race(ResourceName resource_name, TracePosition trace_position, ThreadID thread_id_1, ThreadID thread_id_2);
    public:
        Thread* get_thread(ThreadID thread_id);
        Resource* get_resource(ResourceName resource_name);
        void read_event(ThreadID, TracePosition, ResourceName);
        void write_event(ThreadID, TracePosition, ResourceName);
        void acquire_event(ThreadID, TracePosition, ResourceName);
        void release_event(ThreadID, TracePosition, ResourceName);
        void fork_event(ThreadID, TracePosition, ThreadID);
        void join_event(ThreadID, TracePosition, ThreadID);
        void notify_event(ThreadID, TracePosition, ResourceName);
        void wait_event(ThreadID, TracePosition, ResourceName);
        void get_races();
};

#endif