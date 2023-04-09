#include <vector>
#include <set>
#include <deque>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <memory>
#include <stack>
#include <chrono>
#include <sstream>
#include "detector.hpp"
#include "vectorclock.hpp"
#include "pwrdetector.hpp"

/**
 * Optimized PWR + Undead
 */
class PWRUNDEADDetector : public Detector
{
private:
#ifdef PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT
    const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = PWRUNDEADDETECTOR_VC_PER_DEP_LIMIT;
#else
    const size_t VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT = 5;
#endif

#ifdef COLLECT_STATISTICS
    size_t undead_size_of_all_locksets_count = 0;
    size_t pwrundead_size_of_all_locksets_count = 0;
#endif

    struct LockDependency
    {
        ThreadID id;
        ResourceName resource_name;
        VectorClock *vector_clock;
        const std::set<ResourceName> *lockset;
    };

    struct EpochVCPair
    {
        Epoch epoch;
        VectorClock vector_clock;
    };

    struct EpochLSPair
    {
        Epoch epoch;
        std::set<ResourceName> lockset;
        bool is_write;
    };

    struct Thread
    {
        ThreadID thread_id;
        /**
         * We save all acq vectorclocks that are mapped to (ls, l).
         * We can use a nested hashmap for that --> vectorclocks_collected[ls][l] would return all possible acq(l) VCs for (ls, l).
         */
        std::set<ResourceName> lockset = {};
        std::map<std::set<ResourceName>, std::unordered_map<ResourceName,
                                                            std::deque<VectorClock>>>
            vectorclocks_collected;
        // H(y)
        std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>> history = {};
        // Th(i)
        VectorClock vector_clock = {};
        std::unordered_map<ResourceName, TracePosition> last_read_merges = {};
        std::unordered_map<ResourceName, TracePosition> lock_acquired_at = {};
        TracePosition last_write_at = 0;
    };

    struct Resource
    {
        // RW(x)
        std::vector<EpochLSPair> read_write_events = {};
        // Acq(y)
        Epoch last_acquire = {};
        // L_w(x)
        VectorClock last_write_vc = {};
        // L_wt(x)
        ThreadID last_write_thread = {};
        // L_wl(x)
        std::set<ResourceName> last_write_ls = {};
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
    std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>>
        global_history = {};
    // Global lockset for guard lock detection
    std::set<ResourceName> lockset_global = {};

    /**
     * We have a thread-local history, but other threads need to "know" what happened before they are first encountered,
     * so we copy history from the other thread's
     */
    Thread *get_thread(ThreadID thread_id)
    {
        auto current_thread_ptr = threads.find(thread_id);
        if (current_thread_ptr == threads.end())
        {
            std::unordered_map<ResourceName, std::deque<std::shared_ptr<EpochVCPair>>> new_history = {};
            for (auto history_pair : global_history)
            {
                new_history[history_pair.first] = history_pair.second;
            }

            return &threads.insert({thread_id,
                                    Thread{
                                        thread_id,
                                        {},
                                        {},
                                        new_history,
                                        VectorClock(thread_id),
                                        {},
                                        {},
                                        0}})
                        .first->second;
        }
        else
        {
            return &current_thread_ptr->second;
        }
    }

    Resource *get_resource(ResourceName resource_name)
    {
        auto current_resource_ptr = resources.find(resource_name);
        if (current_resource_ptr == resources.end())
        {
            return &resources.insert(std::make_pair(resource_name, Resource{})).first->second;
        }
        else
        {
            return &current_resource_ptr->second;
        }
    }

    void insert_vectorclock_into_thread(Thread *thread, VectorClock *vc, std::set<ResourceName> *ls, ResourceName l)
    {
#ifdef COLLECT_STATISTICS
        pwrundead_size_of_all_locksets_count += ls->size();
#endif

        auto ls_map = thread->vectorclocks_collected.find(*ls);
        if (ls_map == thread->vectorclocks_collected.end())
        {
            ls_map = thread->vectorclocks_collected.insert({*ls, {}}).first;
        }

        auto l_map = ls_map->second.find(l);
        if (l_map == ls_map->second.end())
        {
            l_map = ls_map->second.insert({l, {}}).first;

#ifdef COLLECT_STATISTICS
            undead_size_of_all_locksets_count += ls->size();
#endif
        }

        if (l_map->second.size() >= this->VECTOR_CLOCKS_PER_DEPENDENCY_LIMIT)
        {
            l_map->second.pop_front();
        }
        l_map->second.push_back(*vc);
    }

    bool isCycleChain(std::vector<LockDependency> *chain_stack, LockDependency *dependency)
    {
        for (auto &chain_dep_ls : *chain_stack->front().lockset)
        {
            if (chain_dep_ls == dependency->resource_name)
                return true;
        }
        return false;
    }

    bool isChain(std::vector<LockDependency> *chain_stack, LockDependency *dependency)
    {
        for (auto &chain_dep : *chain_stack)
        {
            // Check if (LD-3) l_n in ls_1
            if (chain_dep.resource_name == dependency->resource_name)
                return false;
            // Check if (LD-1) LS(ls_i) cap LS(ls_j)
            for (auto &chain_dep_ls : *chain_dep.lockset)
            {
                for (auto &dependency_ls : *dependency->lockset)
                {
                    if (chain_dep_ls == dependency_ls)
                        return false;
                }
            }
        }
        // Check if (LD-2) l_i in ls_i+1 for i=1,...,n-1
        for (auto &dependency_ls : *dependency->lockset)
        {
            if (chain_stack->back().resource_name == dependency_ls)
                return true;
        }
        return false;
    }

    bool isChainVC(std::vector<LockDependency> *chain_stack, LockDependency *dependency)
    {
        for (auto &chain_dep : *chain_stack)
        {
            // Check if (LD-4) l_i_VC || l_j_VC for i != j
            if (chain_dep.vector_clock->less_than(dependency->vector_clock) ||
                dependency->vector_clock->less_than(chain_dep.vector_clock))
            {
                return false;
            }
        }

        return true;
    }

    void dfs(std::vector<LockDependency> *chain_stack, int visiting_thread_id,
             std::unordered_map<ThreadID, bool> *is_traversed, std::set<ThreadID> *thread_ids)
    {
        for (auto thread_id = thread_ids->cbegin(); thread_id != thread_ids->cend(); ++thread_id)
        {
            if (*thread_id <= visiting_thread_id)
                continue;
            Thread *thread = get_thread(*thread_id);
            if (thread->vectorclocks_collected.empty())
                continue;

            if (!is_traversed->find(*thread_id)->second)
            {
                for (auto &d : thread->vectorclocks_collected)
                {
                    for (auto &l : d.second)
                    {
                        bool is_first = true;
                        bool is_cycle_chain = false;
                        for (auto &vc : l.second)
                        {
                            LockDependency dependency = LockDependency{
                                *thread_id,
                                l.first,
                                &vc,
                                &d.first};

                            // If it's not a valid chain for conditions LD-1 to LD-3, we don't have to check the other VC deps
                            if (is_first)
                            {
                                if (!isChain(chain_stack, &dependency))
                                {
                                    break;
                                }
                                is_cycle_chain = isCycleChain(chain_stack, &dependency);
                                is_first = false;
                            }

                            // Only check for LD-4, we already checked for the previous ones
                            if (isChainVC(chain_stack, &dependency))
                            {
                                if (is_cycle_chain)
                                {
                                    this->lockframe->report_race(
                                        DataRace{dependency.resource_name, 0, chain_stack->front().id,
                                                 dependency.id});
                                }
                                else
                                {
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
    }

    // This function was originally called w3 in the paper.
    // In the mean time, the algorithm was renamed from w3po to PWR.
    void pwr_history_sync(Thread *thread, Resource *resource)
    {
        for (ResourceName lock : thread->lockset)
        {
            auto current_history_iter = thread->history.find(lock);
            if (current_history_iter == thread->history.end())
                continue;
            auto current_history = &current_history_iter->second;

            for (auto epoch_vc_pair_iter = current_history->begin(); epoch_vc_pair_iter != current_history->end();)
            {
                auto thread_id_j = epoch_vc_pair_iter->get()->epoch.thread_id;
                auto value_k = epoch_vc_pair_iter->get()->epoch.value;
                auto vc_dash = &epoch_vc_pair_iter->get()->vector_clock;
                auto vc_value_at_j = thread->vector_clock.find(thread_id_j);
                auto vc_dash_value_at_j = vc_dash->find(thread_id_j);

                if (vc_dash_value_at_j <= vc_value_at_j)
                {
                    epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
                }
                else
                {
                    if (value_k < vc_value_at_j)
                    {
                        thread->vector_clock.merge_into(vc_dash);
                        epoch_vc_pair_iter = current_history->erase(epoch_vc_pair_iter);
                    }
                    else
                    {
                        ++epoch_vc_pair_iter;
                    }
                }
            }
        }
    }

    // RW = { (i#Th(i)[i], LS_t(i) } U { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
    void update_read_write_events(Thread *thread, Resource *resource, bool is_write)
    {
        // (i#Th(i)[i], LS_t(i))
        // Current "timestamp" of the calling thread with its current locks.

        // { (j#k, L) | (j#k, L) e RW(x) AND k > Th(i)[i] }
        // Find everything in RW(x) where thread_2's epoch value is higher than the vector clock of thread_id is set at thread_2.
        for (auto rw_pair_iter = resource->read_write_events.begin();
             rw_pair_iter != resource->read_write_events.end();)
        {
            if (rw_pair_iter->epoch.value <= thread->vector_clock.find(rw_pair_iter->epoch.thread_id) &&
                !(!is_write && rw_pair_iter->is_write))
            {
                rw_pair_iter = resource->read_write_events.erase(rw_pair_iter);
            }
            else
            {
                ++rw_pair_iter;
            }
        }

        resource->read_write_events.push_back(
            EpochLSPair{Epoch{thread->thread_id, thread->vector_clock.find(thread->thread_id)}, thread->lockset,
                        is_write});
    }

    bool check_locksets_overlap(std::vector<ResourceName> *ls1, std::vector<ResourceName> *ls2)
    {
        for (ResourceName rn_t1 : *ls1)
        {
            for (ResourceName rn_t2 : *ls2)
            {
                if (rn_t1 == rn_t2)
                {
                    return true;
                }
            }
        }

        return false;
    }

    void find_cycles()
    {
        std::set<ThreadID> thread_ids = {};
        std::unordered_map<ThreadID, bool> is_traversed = {};
        for (auto const &thread : threads)
        {
            thread_ids.insert(thread.first);
            is_traversed[thread.first] = false;
        }

        int visiting;
        std::vector<LockDependency> chain_stack = {};
        for (auto thread_id = thread_ids.cbegin(); thread_id != thread_ids.cend(); ++thread_id)
        {
            Thread *thread = get_thread(*thread_id);
            if (thread->vectorclocks_collected.empty())
                continue;
            visiting = *thread_id;

            for (auto &d : thread->vectorclocks_collected)
            {
                for (auto &l : d.second)
                {
                    for (auto &vc : l.second)
                    {
                        is_traversed[*thread_id] = true;
                        chain_stack.push_back(LockDependency{
                            *thread_id,
                            l.first,
                            &vc,
                            &d.first});
                        dfs(&chain_stack, visiting, &is_traversed, &thread_ids);
                        chain_stack.pop_back();
                    }
                }
            }
        }
    }

public:
    void read_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);
        Resource *resource = get_resource(resource_name);

        if (resource->last_write_occured)
        {
            auto last_read_merge = thread->last_read_merges.find(resource_name);
            bool first_read_after_write = last_read_merge == thread->last_read_merges.end() ||
                                          last_read_merge->second < resource->last_write_occured_at;
            if (first_read_after_write)
            {
                thread->last_read_merges[resource_name] = resource->last_write_occured_at;
            }

            if (first_read_after_write)
            {
                // Th(i) = Th(i) |_| L_w(x)
                // "Compare values per thread, set to max ==> merge operation"
                thread->vector_clock.merge_into(&resource->last_write_vc);

                pwr_history_sync(thread, resource);
            }
        }

        update_read_write_events(thread, resource, false);

        thread->vector_clock.increment(thread_id);
    }

    void write_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);
        Resource *resource = get_resource(resource_name);

        update_read_write_events(thread, resource, true);

        resource->last_write_vc = thread->vector_clock;
        resource->last_write_thread = thread_id;
        resource->last_write_ls = thread->lockset;
        resource->last_write_occured = true;
        resource->last_write_occured_at = trace_position;
        thread->last_write_at = trace_position;

        thread->vector_clock.increment(thread_id);
    }

    void acquire_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);
        Resource *resource = get_resource(resource_name);

        pwr_history_sync(thread, resource);

        insert_vectorclock_into_thread(
            thread,
            &thread->vector_clock,
            &thread->lockset,
            resource_name);

        // Add Resource to Lockset, using std::set can't add multiple times
        thread->lockset.insert(resource_name);
        this->lockset_global.insert(resource_name);

        // Set acquire History
        resource->last_acquire = Epoch{thread_id, thread->vector_clock.find(thread_id)};

        thread->lock_acquired_at[resource_name] = trace_position;

        thread->vector_clock.increment(thread_id);
    }

    void release_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);
        Resource *resource = get_resource(resource_name);

        pwr_history_sync(thread, resource);

        // Remove Resource from Lockset
        thread->lockset.erase(resource_name);
        this->lockset_global.erase(resource_name);

        auto lock_acquired_at = thread->lock_acquired_at.find(resource_name);

        // Add to history
        // Adds EpochVCPair to thread's history and global history.
        // Limits history size per mutex to THREAD_HISTORY_SIZE (see at top of file).
        if (lock_acquired_at != thread->lock_acquired_at.end() && lock_acquired_at->second < thread->last_write_at)
        {
            std::shared_ptr<EpochVCPair> shared_epoch_vc_pair = std::shared_ptr<EpochVCPair>(new EpochVCPair{
                resource->last_acquire,
                thread->vector_clock});
            for (auto thread_iter = threads.begin(); thread_iter != threads.end(); ++thread_iter)
            {
                if (thread_iter->first == thread_id)
                {
                    continue;
                }

                auto current_history = &thread_iter->second.history.emplace(std::piecewise_construct,
                                                                            std::forward_as_tuple(resource_name),
                                                                            std::forward_as_tuple())
                                            .first->second;
                if (current_history->size() >= THREAD_HISTORY_SIZE)
                {
                    current_history->pop_back();
                }
                current_history->push_front(shared_epoch_vc_pair);
            }

            auto current_global_history = &global_history.emplace(std::piecewise_construct,
                                                                  std::forward_as_tuple(resource_name),
                                                                  std::forward_as_tuple())
                                               .first->second;
            if (current_global_history->size() >= THREAD_HISTORY_SIZE)
            {
                current_global_history->pop_back();
            }
            current_global_history->push_front(shared_epoch_vc_pair);
        }

        thread->vector_clock.increment(thread_id);
    }

    void fork_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id)
    {
        Thread *thread = get_thread(thread_id);
        Thread *target_thread = get_thread(target_thread_id);

        target_thread->vector_clock = thread->vector_clock;
        target_thread->vector_clock.increment(target_thread_id);

        thread->vector_clock.increment(thread_id);
    }

    void join_event(ThreadID thread_id, TracePosition trace_position, ThreadID target_thread_id)
    {
        Thread *thread = get_thread(thread_id);
        Thread *target_thread = get_thread(target_thread_id);

        thread->vector_clock.merge_into(&target_thread->vector_clock);

        thread->vector_clock.increment(thread_id);
    }

    void notify_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);

        auto notifies_iter = notifies.find(resource_name);
        if (notifies_iter == notifies.end())
        {
            notifies_iter = notifies.insert({resource_name, VectorClock()}).first;
        }

        notifies_iter->second.merge_into(&thread->vector_clock);
        thread->vector_clock.merge_into(&notifies_iter->second);

        thread->vector_clock.increment(thread_id);
    }

    void wait_event(ThreadID thread_id, TracePosition trace_position, ResourceName resource_name)
    {
        Thread *thread = get_thread(thread_id);

        auto notifies_iter = notifies.find(resource_name);

        if (notifies_iter == notifies.end())
        {
            return;
        }

        thread->vector_clock.merge_into(&notifies_iter->second);
        thread->vector_clock.increment(thread_id);

        notifies[resource_name] = thread->vector_clock;
    }

    void get_races()
    {
#ifdef COLLECT_STATISTICS
        size_t undead_dependency_count = 0;
        std::vector<size_t> undead_dependency_per_thread_count = {};
        size_t pwrundead_dependency_count = 0;
        std::vector<size_t> pwrundead_dependency_per_thread_count = {};

        for (auto i : threads)
        {
            size_t dep_counter = 0;
            size_t vc_dep_counter = 0;
            for (auto &d : i.second.vectorclocks_collected)
            {
                for (auto &l : d.second)
                {
                    dep_counter += 1;
                    for (auto &vc : l.second)
                    {
                        vc_dep_counter += 1;
                    }
                }
            }

            undead_dependency_count += dep_counter;
            undead_dependency_per_thread_count.push_back(dep_counter);
            pwrundead_dependency_count += vc_dep_counter;
            pwrundead_dependency_per_thread_count.push_back(vc_dep_counter);
        }

        this->lockframe->report_statistic("UNDEAD dependencies sum",
                                          undead_dependency_count);

        this->lockframe->report_statistic("UNDEAD dependencies per thread",
                                          undead_dependency_per_thread_count);

        this->lockframe->report_statistic("PWRUNDEAD dependencies sum",
                                          pwrundead_dependency_count);

        this->lockframe->report_statistic("PWRUNDEAD dependencies per thread",
                                          pwrundead_dependency_per_thread_count);

        this->lockframe->report_statistic("UNDEAD size of all locksets",
                                          undead_size_of_all_locksets_count);

        this->lockframe->report_statistic("PWRUNDEAD size of all locksets",
                                          pwrundead_size_of_all_locksets_count);
        auto start = std::chrono::steady_clock::now();
#endif

        find_cycles();

#ifdef COLLECT_STATISTICS
        auto end = std::chrono::steady_clock::now();
        this->lockframe->report_statistic("Phase 2 elapsed time in milliseconds",
                                          std::to_string(
                                              std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()));
#endif
    }
};