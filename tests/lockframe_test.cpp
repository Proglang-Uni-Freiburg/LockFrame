// "PaperExample..." from https://arxiv.org/pdf/2004.06969.pdf

#include <gtest/gtest.h>
#include "../lockframe.hpp"
#include "../pwrdetector.hpp"
#include "../undead.hpp"
#include <chrono>
#include <iostream>

void compare_races(DataRace race1, DataRace race2) {
    ASSERT_EQ(race1.resource_name, race2.resource_name);
    ASSERT_EQ(race1.trace_position, race2.trace_position);
    ASSERT_EQ(race1.thread_id_1, race2.thread_id_1);
    ASSERT_EQ(race1.thread_id_2, race2.thread_id_2);
}

LockFrame* get_pwr_lockframe() {
    LockFrame* lockFrame = new LockFrame();
    PWRDetector* pwrDetector = new PWRDetector();
    lockFrame->set_detector(pwrDetector);
    return lockFrame;
}

LockFrame* get_undead_lockframe() {
    LockFrame* lockFrame = new LockFrame();
    UNDEADDetector* undeadDetector = new UNDEADDetector();
    lockFrame->set_detector(undeadDetector);
    return lockFrame;
}

void paper_example_one(LockFrame* lockFrame) {
    lockFrame->write_event(1, 1, 1);
    lockFrame->acquire_event(1, 2, 2);
    lockFrame->release_event(1, 3, 2);
    lockFrame->acquire_event(2, 4, 2);
    lockFrame->write_event(2, 5, 1);
    lockFrame->release_event(2, 6, 2);
    ASSERT_EQ(lockFrame->get_races().size(), 1);
    compare_races(lockFrame->get_races().at(0), DataRace{1, 5, 2, 1});
}

void paper_example_six(LockFrame* lockFrame) {
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    lockFrame->acquire_event(1, 1, 3);
    lockFrame->write_event(1, 2, 4);
    lockFrame->write_event(1, 3, 1);
    lockFrame->release_event(1, 4, 3);
    lockFrame->read_event(2, 5, 4);
    lockFrame->write_event(2, 6, 5);
    lockFrame->acquire_event(3, 7, 3);
    lockFrame->read_event(3, 8, 5);
    lockFrame->release_event(3, 9, 3);
    lockFrame->write_event(3, 10, 1);

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[µs]" << std::endl;
    std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count() << "[ns]" << std::endl;

    ASSERT_EQ(lockFrame->get_races().size(), 2);
}

void paper_example_six_nowrd(LockFrame* lockFrame) {
    lockFrame->acquire_event(0,3, 3);
    lockFrame->acquire_event(0,4, 7);
    lockFrame->write_event(0,5, 4);
    lockFrame->release_event(0,6, 7);
    lockFrame->write_event(0,7, 1);
    lockFrame->release_event(0,8, 3);
    lockFrame->acquire_event(1,9, 7);
    lockFrame->read_event(1,10, 4);
    lockFrame->release_event(1,11, 7);
    lockFrame->acquire_event(1,12, 8);
    lockFrame->write_event(1,13, 5);
    lockFrame->release_event(1,14, 8);
    lockFrame->acquire_event(2,15, 3);
    lockFrame->acquire_event(2,16, 8);
    lockFrame->read_event(2,17, 5);
    lockFrame->release_event(2,18, 8);
    lockFrame->release_event(2,19, 3);
    lockFrame->write_event(2,20, 1);
    ASSERT_EQ(lockFrame->get_races().size(), 0);
}

// This is an optimization problem.
// Normally, there's a race between Trace#1 and Trace#6, but we can't detect it because
// the event is filtered from RW() after Trace#3.
void paper_example_eight(LockFrame* lockFrame) {
    lockFrame->write_event(1, 1, 1);
    lockFrame->acquire_event(1, 2, 2);
    lockFrame->write_event(1, 3, 1);
    lockFrame->release_event(1, 4, 2);
    lockFrame->acquire_event(2, 5, 2);
    lockFrame->write_event(2, 6, 1);
    lockFrame->release_event(2, 7, 2);
    ASSERT_EQ(lockFrame->get_races().size(), 0); 
}

TEST(LockFramePWRTest, PaperExampleOne) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_one(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_6) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_six(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_6_nowrd) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_six_nowrd(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_8) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_eight(lockFrame);
}

TEST(LockFrameUNDEADTest, Test1) {
    LockFrame* lockFrame = get_undead_lockframe();

    lockFrame->acquire_event(1, 1, 1);
    lockFrame->acquire_event(1, 2, 2);
    lockFrame->release_event(1, 3, 2);
    lockFrame->release_event(1, 4, 1);
    lockFrame->acquire_event(2, 5, 2);
    lockFrame->acquire_event(2, 6, 1);
    lockFrame->release_event(2, 7, 1);
    lockFrame->release_event(2, 8, 2);

    ASSERT_EQ(lockFrame->get_races().size(), 1);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}