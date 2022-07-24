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
            std::map<std::set<ResourceName>, std::set<ResourceName>> dependencies;
        };

        struct LockDependency {
            ThreadID id;
            ResourceName resource_name;
            std::set<ResourceName> lockset;

            bool operator==(const LockDependency& a) const {
                return id == a.id && resource_name == a.resource_name && lockset == a.lockset;
            }

            bool operator<(const LockDependency& a) const {
                if(id < a.id) {
                    return true;
                } else if(a.id != id) {
                    return false;
                }

                if(resource_name < a.resource_name) {
                    return true;
                } else if(a.resource_name != resource_name) {
                    return false;
                }

                if(lockset < a.lockset) {
                    return true;
                } else if(a.lockset != lockset) {
                    return false;
                }
                
                return false;
            }
        };

        std::unordered_map<ThreadID, Thread> threads = {};

        bool isOrderedDependencyInChain(const std::vector<LockDependency>* dependencyChain);
        bool check_locksets_overlap(std::set<ResourceName> ls1, std::set<ResourceName> ls2);
        bool check_lockset_in_lockset(std::set<ResourceName> lockset_small, std::set<ResourceName> lockset_big);
        bool dependencyChainLocksetsOverlapForAny(const std::vector<LockDependency>* dependencyChain);
        bool dependencyChainLockInLocksetsCycle(const std::vector<LockDependency>* dependencyChain);
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