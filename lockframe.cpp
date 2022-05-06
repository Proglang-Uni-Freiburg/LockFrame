#include <vector>
#include "lockframe.hpp"
#include "detector.hpp"

Detector* detector;
std::vector<DataRace> races = {};

void LockFrame::set_detector(Detector* d) {
    d->lockframe = this;
    detector = d;
}

void LockFrame::read_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->read_event(tid, pos, name);
}

void LockFrame::write_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->write_event(tid, pos, name);
}

void LockFrame::aquire_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->aquire_event(tid, pos, name);
}

void LockFrame::release_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->release_event(tid, pos, name);
}

void LockFrame::fork_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->fork_event(tid, pos, name);
}

void LockFrame::join_event(ThreadID tid, TracePosition pos, ResourceName name) {
    detector->join_event(tid, pos, name);
}

void LockFrame::report_race(DataRace race) {
    printf("\n---\nPOTENTIAL RACE FOUND %s@%d: T%d<-->T%d\n---\n", race.resource_name.c_str(), race.trace_position, race.thread_id_1, race.thread_id_2);
    races.push_back(race);
}

std::vector<DataRace> LockFrame::get_races() {
    return races;
}