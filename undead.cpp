#include "undead.hpp"

bool UNDEADDetector::isOrderedDependencyInChain(const std::vector<LockDependency>* dependencyChain) {
    for(auto dependencyIter = dependencyChain->begin(); dependencyIter != dependencyChain->end(); ++dependencyIter) {
        if(std::next(dependencyIter) != dependencyChain->end()) {
            if(std::next(dependencyIter)->id <= dependencyChain->begin()->id) {
                return false;
            }
        }
    }

    return true;
}

bool UNDEADDetector::check_locksets_overlap(std::set<ResourceName> ls1, std::set<ResourceName> ls2) {
    for(ResourceName rn_t1 : ls1) {
        for(ResourceName rn_t2 : ls2) {
            if(rn_t1 == rn_t2) {
                return true;
            }
        }
    }

    return false;
}

bool UNDEADDetector::check_lockset_in_lockset(std::set<ResourceName> lockset_small, std::set<ResourceName> lockset_big) {
    for(ResourceName rn_t1 : lockset_small) {
        if(lockset_big.find(rn_t1) == lockset_big.end()) {
            return false;
        }
    }

    return true;
}

bool UNDEADDetector::dependencyChainLocksetsOverlapForAny(const std::vector<LockDependency>* dependencyChain) {
    for(auto dependencyIter1 = std::next(dependencyChain->begin()); dependencyIter1 != dependencyChain->end(); ++dependencyIter1) {
        for(auto dependencyIter2 = std::next(dependencyChain->begin()); dependencyIter2 != dependencyChain->end(); ++dependencyIter2) {
            // Skip duplicates
            if(dependencyIter1 != dependencyIter2 && check_locksets_overlap(dependencyIter1->lockset, dependencyIter2->lockset)) {
                return true;
            }
        }
    }

    return false;
}

bool UNDEADDetector::dependencyChainLockInLocksetsCycle(const std::vector<LockDependency>* dependencyChain) {
    for(auto dependencyIter = std::next(dependencyChain->begin()); dependencyIter != dependencyChain->end(); ++dependencyIter) {
        if(std::next(dependencyIter) == dependencyChain->end()) {
            // We are in last element of vector, check with first (LD-3)
            if(dependencyChain->begin()->lockset.find(dependencyIter->resource_name) == dependencyChain->begin()->lockset.end()) {
                return false;
            }
        } else {
            // LD-2
            //if(!check_lockset_in_lockset(dependencyIter->lockset, std::next(dependencyIter)->lockset)) {
            if(std::next(dependencyIter)->lockset.find(dependencyIter->resource_name) == std::next(dependencyIter)->lockset.end()) {
                return false;
            }
        }
    }

    return true;
}

void UNDEADDetector::find_cycles() {
    std::vector<LockDependency> allDependencies = {};

    for(auto &t: threads) {
        for(auto &d: t.second.dependencies) {
            for(auto &l: d.second) {
                allDependencies.push_back(LockDependency {
                    t.second.id,
                    l,
                    d.first
                });
            }
        }
    }

    std::sort(allDependencies.begin(), allDependencies.end());

    for(int n = 2; n <= std::min(threads.size(), allDependencies.size()); n++) {
        // D_i
        std::set<std::vector<LockDependency>> permutations = {};

        do {
            permutations.emplace(allDependencies.begin(), allDependencies.begin() + n);
        } while(std::next_permutation(allDependencies.begin(), allDependencies.end()));

        for(auto &dependencyChain: permutations) {
            if(!isOrderedDependencyInChain(&dependencyChain) ||
                dependencyChainLocksetsOverlapForAny(&dependencyChain) ||
                !dependencyChainLockInLocksetsCycle(&dependencyChain)
            ) continue;

            if(this->lockframe != NULL) {
                this->lockframe->report_race(DataRace{ dependencyChain.back().resource_name, 0, dependencyChain.front().id, dependencyChain.back().id });
            }
        }
    }
}
        
UNDEADDetector::Thread* UNDEADDetector::get_thread(ThreadID thread_id) {
    auto current_thread_ptr = threads.find(thread_id);
    if(current_thread_ptr == threads.end()) {
        return &threads.insert({
            thread_id,
            Thread {
                thread_id,
                {}
            }
        }).first->second;
    } else {
        return &current_thread_ptr->second;
    }
}

void UNDEADDetector::acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    Thread* thread = get_thread(thread_id);

    auto dependency_resources = &thread->dependencies.emplace(std::piecewise_construct, std::forward_as_tuple(thread->lockset), std::forward_as_tuple()).first->second;
    // only adds to set if doesn't already exist
    dependency_resources->insert(resource_name);

    // push l to LockSet[t]
    thread->lockset.insert(resource_name);
}

void UNDEADDetector::release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
    Thread* thread = get_thread(thread_id);

    thread->lockset.erase(resource_name);
}

void UNDEADDetector::read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
}

void UNDEADDetector::write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {
}

void UNDEADDetector::fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
}

void UNDEADDetector::join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id) {
}

void UNDEADDetector::notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {}
void UNDEADDetector::wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name) {}

void UNDEADDetector::get_races() {
    find_cycles();
}