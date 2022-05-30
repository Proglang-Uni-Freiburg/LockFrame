// "PaperExample..." from https://arxiv.org/pdf/2004.06969.pdf

#include <gtest/gtest.h>
#include "../lockframe.hpp"
#include "../pwrdetector.cpp"
#include "../pwrdetector_optimized.cpp"
#include "../undead.cpp"
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

LockFrame* get_pwr_optimized_lockframe() {
    LockFrame* lockFrame = new LockFrame();
    PWRDetectorOptimized* pwrOptimizedDetector = new PWRDetectorOptimized();
    lockFrame->set_detector(pwrOptimizedDetector);
    return lockFrame;
}

LockFrame* get_undead_lockframe() {
    LockFrame* lockFrame = new LockFrame();
    UNDEADDetector* undeadDetector = new UNDEADDetector();
    lockFrame->set_detector(undeadDetector);
    return lockFrame;
}

void paper_example_one(LockFrame* lockFrame) {
    lockFrame->write_event(1, 1, std::string("x"));
    lockFrame->acquire_event(1, 2, std::string("y"));
    lockFrame->release_event(1, 3, std::string("y"));
    lockFrame->acquire_event(2, 4, std::string("y"));
    lockFrame->write_event(2, 5, std::string("x"));
    lockFrame->release_event(2, 6, std::string("y"));
    ASSERT_EQ(lockFrame->get_races().size(), 1);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("x"), 5, 2, 1});
}

void paper_example_two(LockFrame* lockFrame) {
    lockFrame->read_event(1, 1, std::string("y"));
    lockFrame->read_event(1, 2, std::string("x"));
    lockFrame->write_event(2, 3, std::string("y"));
    lockFrame->write_event(2, 4, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 2);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("y"), 3, 2, 1});
    compare_races(lockFrame->get_races().at(1), DataRace{std::string("x"), 4, 2, 1});
}

void paper_example_three(LockFrame* lockFrame) {
    lockFrame->acquire_event(1, 1, std::string("y'"));
    lockFrame->read_event(1, 2, std::string("y"));
    lockFrame->release_event(1, 3, std::string("y'"));
    lockFrame->read_event(1, 4, std::string("x"));
    lockFrame->acquire_event(2, 5, std::string("y'"));
    lockFrame->write_event(2, 6, std::string("y"));
    lockFrame->release_event(2, 7, std::string("y'"));
    lockFrame->write_event(2, 8, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 1);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("x"), 8, 2, 1});
}

void paper_example_six(LockFrame* lockFrame) {
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    lockFrame->acquire_event(1, 1, std::string("z"));
    lockFrame->write_event(1, 2, std::string("y1"));
    lockFrame->write_event(1, 3, std::string("x"));
    lockFrame->release_event(1, 4, std::string("z"));
    lockFrame->read_event(2, 5, std::string("y1"));
    lockFrame->write_event(2, 6, std::string("y2"));
    lockFrame->acquire_event(3, 7, std::string("z"));
    lockFrame->read_event(3, 8, std::string("y2"));
    lockFrame->release_event(3, 9, std::string("z"));
    lockFrame->write_event(3, 10, std::string("x"));

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[µs]" << std::endl;
    std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count() << "[ns]" << std::endl;

    ASSERT_EQ(lockFrame->get_races().size(), 2);
}

void paper_example_six_nowrd(LockFrame* lockFrame) {
    lockFrame->acquire_event(0,3, std::string("z"));
    lockFrame->acquire_event(0,4, std::string("z1"));
    lockFrame->write_event(0,5, std::string("y1"));
    lockFrame->release_event(0,6, std::string("z1"));
    lockFrame->write_event(0,7, std::string("x"));
    lockFrame->release_event(0,8, std::string("z"));
    lockFrame->acquire_event(1,9, std::string("z1"));
    lockFrame->read_event(1,10, std::string("y1"));
    lockFrame->release_event(1,11, std::string("z1"));
    lockFrame->acquire_event(1,12, std::string("z2"));
    lockFrame->write_event(1,13, std::string("y2"));
    lockFrame->release_event(1,14, std::string("z2"));
    lockFrame->acquire_event(2,15, std::string("z"));
    lockFrame->acquire_event(2,16, std::string("z2"));
    lockFrame->read_event(2,17, std::string("y2"));
    lockFrame->release_event(2,18, std::string("z2"));
    lockFrame->release_event(2,19, std::string("z"));
    lockFrame->write_event(2,20, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 0);
}

// This is an optimization problem.
// Normally, there's a race between Trace#1 and Trace#6, but we can't detect it because
// the event is filtered from RW() after Trace#3.
void paper_example_eight(LockFrame* lockFrame) {
    lockFrame->write_event(1, 1, std::string("x"));
    lockFrame->acquire_event(1, 2, std::string("y"));
    lockFrame->write_event(1, 3, std::string("x"));
    lockFrame->release_event(1, 4, std::string("y"));
    lockFrame->acquire_event(2, 5, std::string("y"));
    lockFrame->write_event(2, 6, std::string("x"));
    lockFrame->release_event(2, 7, std::string("y"));
    ASSERT_EQ(lockFrame->get_races().size(), 0); 
}

TEST(LockFramePWRTest, PaperExampleOne) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_one(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExampleOne) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_one(lockFrame);
}

TEST(LockFramePWRTest, PaperExampleTwo) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_two(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExampleTwo) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_two(lockFrame);
}

TEST(LockFramePWRTest, PaperExampleThree) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_three(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExampleThree) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_three(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_6) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_six(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExample_2_6) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_six(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_6_nowrd) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_six_nowrd(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExample_2_6_nowrd) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_six_nowrd(lockFrame);
}

TEST(LockFramePWRTest, PaperExample_2_8) {
    LockFrame* lockFrame = get_pwr_lockframe();
    paper_example_eight(lockFrame);
}

TEST(LockFramePWROptimizedTest, PaperExample_2_8) {
    LockFrame* lockFrame = get_pwr_optimized_lockframe();
    paper_example_eight(lockFrame);
}

TEST(LockFrameUNDEADTest, Test1) {
    LockFrame* lockFrame = get_undead_lockframe();

    lockFrame->acquire_event(1, 1, std::string("l1"));
    lockFrame->acquire_event(1, 2, std::string("l2"));
    lockFrame->release_event(1, 3, std::string("l2"));
    lockFrame->release_event(1, 4, std::string("l1"));
    lockFrame->acquire_event(2, 5, std::string("l2"));
    lockFrame->acquire_event(2, 6, std::string("l1"));
    lockFrame->release_event(2, 7, std::string("l1"));
    lockFrame->release_event(2, 8, std::string("l2"));

    ASSERT_EQ(lockFrame->get_races().size(), 1);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}