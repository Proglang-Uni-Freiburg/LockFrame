#include "undead.hpp"
#include <chrono>

void UNDEADDetector::find_cycles() {
    std::set<ThreadID> thread_ids = {};
    std::unordered_map<ThreadID, bool> is_traversed = {};
    for(auto const& thread: threads) {
        thread_ids.insert(thread.first);
        is_traversed[thread.first] = false;
    }

    int visiting;
    std::vector<LockDependency> chain_stack = {};
    for(auto thread_id = thread_ids.cbegin(); thread_id != thread_ids.cend(); ++thread_id) {
        Thread* thread = get_thread(*thread_id);
        if(thread->dependencies.empty()) continue;
        visiting = *thread_id;

        for(auto &d: thread->dependencies) {
            for(auto &l: d.second) {
                is_traversed[*thread_id] = true;
                chain_stack.push_back(LockDependency {
                    *thread_id,
                    l.first,
                    &d.first
                });
                dfs(&chain_stack, visiting, &is_traversed, &thread_ids);
                chain_stack.pop_back();
            }
        }
    }
}

void UNDEADDetector::dfs(std::vector<LockDependency>* chain_stack, int visiting_thread_id, std::unordered_map<ThreadID, bool>* is_traversed, std::set<ThreadID>* thread_ids) {
    for(auto thread_id = thread_ids->cbegin(); thread_id != thread_ids->cend(); ++thread_id) {
        if(*thread_id <= visiting_thread_id) continue;
        Thread* thread = get_thread(*thread_id);
        if(thread->dependencies.empty()) continue;

        if(!is_traversed->find(*thread_id)->second) {
            for(auto &d: thread->dependencies) {
                for(auto &l: d.second) {
                    LockDependency dependency = LockDependency {
                        *thread_id,
                        l.first,
                        &d.first
                    };

                    if(isChain(chain_stack, &dependency)) {
                        if(isCycleChain(chain_stack, &dependency)) {
                            this->lockframe->report_race(DataRace{ dependency.resource_name, 0, chain_stack->front().id, dependency.id });
                        } else {
                            (*is_traversed)[*thread_id] = true;
                            chain_stack->push_back(dependency);
                            dfs(chain_stack, visiting_thread_id, is_traversed, thread_ids);
                            chain_stack->pop_back();
                            (*is_traversed)[*thread_id] = false;
                        }
                    }
                }
            }
        }
    }
}

bool UNDEADDetector::isChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency) {
    for(auto &chain_dep: *chain_stack) {
        // Check if (LD-3) l_n in ls_1
        if(chain_dep.resource_name == dependency->resource_name) return false;
        // Check if (LD-1) LS(ls_i) cap LS(ls_j)
        for(auto &chain_dep_ls: *chain_dep.lockset) {
            for(auto &dependency_ls: *dependency->lockset) {
                if(chain_dep_ls == dependency_ls) return false;
            }
        }
    }
    // Check if (LD-2) l_i in ls_i+1 for i=1,...,n-1
    for(auto &dependency_ls: *dependency->lockset) {
        if(chain_stack->back().resource_name == dependency_ls) return true;
    }
    return false;
}

bool UNDEADDetector::isCycleChain(std::vector<LockDependency>* chain_stack, LockDependency* dependency) {
    for(auto &chain_dep_ls: *chain_stack->front().lockset) {
        if(chain_dep_ls == dependency->resource_name) return true;
    }
    return false;
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

    auto ls_map = thread->dependencies.find(thread->lockset);
    if(ls_map == thread->dependencies.end()) {
        ls_map = thread->dependencies.insert({thread->lockset, {}}).first;
    }

    ls_map->second.insert({ resource_name, true });

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
    #ifdef COLLECT_STATISTICS
        auto start = std::chrono::steady_clock::now();
    #endif

    find_cycles();

    #ifdef COLLECT_STATISTICS
        auto end = std::chrono::steady_clock::now();

        printf("Phase 2 elapsed time in milliseconds: %d\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    #endif
}