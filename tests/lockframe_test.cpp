// "PaperExample..." from https://arxiv.org/pdf/2004.06969.pdf

#include <gtest/gtest.h>
#include "../lockframe.hpp"
#include "../pwrdetector.cpp"

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

TEST(LockFramePWRTest, PaperExampleOne) {
    LockFrame* lockFrame = get_pwr_lockframe();
    lockFrame->write_event(1, 1, std::string("x"));
    lockFrame->aquire_event(1, 2, std::string("y"));
    lockFrame->release_event(1, 3, std::string("y"));
    lockFrame->aquire_event(2, 4, std::string("y"));
    lockFrame->write_event(2, 5, std::string("x"));
    lockFrame->release_event(2, 6, std::string("y"));
    ASSERT_EQ(lockFrame->get_races().size(), 1);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("x"), 5, 2, 1});
}

TEST(LockFramePWRTest, PaperExampleTwo) {
    LockFrame* lockFrame = get_pwr_lockframe();
    lockFrame->read_event(1, 1, std::string("y"));
    lockFrame->read_event(1, 2, std::string("x"));
    lockFrame->write_event(2, 3, std::string("y"));
    lockFrame->write_event(2, 4, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 2);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("y"), 3, 2, 1});
    compare_races(lockFrame->get_races().at(1), DataRace{std::string("x"), 4, 2, 1});
}

TEST(LockFramePWRTest, PaperExampleThree) {
    LockFrame* lockFrame = get_pwr_lockframe();
    lockFrame->aquire_event(1, 1, std::string("y'"));
    lockFrame->read_event(1, 2, std::string("y"));
    lockFrame->release_event(1, 3, std::string("y'"));
    lockFrame->read_event(1, 4, std::string("x"));
    lockFrame->aquire_event(2, 5, std::string("y'"));
    lockFrame->write_event(2, 6, std::string("y"));
    lockFrame->release_event(2, 7, std::string("y'"));
    lockFrame->write_event(2, 8, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 1);
    compare_races(lockFrame->get_races().at(0), DataRace{std::string("x"), 8, 2, 1});
}

TEST(LockFramePWRTest, PaperExample_2_6) {
    LockFrame* lockFrame = get_pwr_lockframe();
    lockFrame->aquire_event(1, 1, std::string("z"));
    lockFrame->write_event(1, 2, std::string("y1"));
    lockFrame->write_event(1, 3, std::string("x"));
    lockFrame->release_event(1, 4, std::string("z"));
    lockFrame->read_event(2, 5, std::string("y1"));
    lockFrame->write_event(2, 6, std::string("y2"));
    lockFrame->aquire_event(3, 7, std::string("z"));
    lockFrame->read_event(3, 8, std::string("y2"));
    lockFrame->release_event(3, 9, std::string("z"));
    lockFrame->write_event(3, 10, std::string("x"));
    ASSERT_EQ(lockFrame->get_races().size(), 2);
}

// This is an optimization problem.
// Normally, there's a race between Trace#1 and Trace#6, but we can't detect it because
// the event is filtered from RW() after Trace#3.
TEST(LockFramePWRTest, PaperExample_2_8) {
    LockFrame* lockFrame = get_pwr_lockframe();
    lockFrame->write_event(1, 1, std::string("x"));
    lockFrame->aquire_event(1, 2, std::string("y"));
    lockFrame->write_event(1, 3, std::string("x"));
    lockFrame->release_event(1, 4, std::string("y"));
    lockFrame->aquire_event(2, 5, std::string("y"));
    lockFrame->write_event(2, 6, std::string("x"));
    lockFrame->release_event(2, 7, std::string("y"));
    ASSERT_EQ(lockFrame->get_races().size(), 0); 
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}