#ifndef UNDEAD_H
#define UNDEAD_H

#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <algorithm>
#include "detector.hpp"
#include "vectorclock.hpp"

class LockFrame;
class UNDEADDetector : public Detector {
    private:
        struct Thread {
            ThreadID id;
            std::set<ResourceName> lockset = {};
            // Use map to find LockDependency faster.
            // We basically keep track of all unique locksets in D and then have a set that contains all resources that
            // were acquired when this lockset was in place.
            std::map<std::set<ResourceName>, std::unordered_map<ResourceName, bool>> dependencies;
        };

        struct LockDependency {
            ThreadID id;
            ResourceName resource_name;
            const std::set<ResourceName>* lockset;
        };

        std::unordered_map<ThreadID, Thread> threads = {};

        void dfs(std::vector<LockDependency>* chain_stack, int visiting_thread_id, std::unordered_map<ThreadID, bool>* is_traversed, std::set<ThreadID>* thread_ids);
        bool isChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency);
        bool isCycleChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency);
        void find_cycles();
    public:
        Thread* get_thread(ThreadID thread_id);
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