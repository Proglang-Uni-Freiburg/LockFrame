# LockFrame

LockFrame is a framework that allows you to implement multiple dynamic deadlock/data race detectors into one interface.

Detectors can easily be swapped inside the code in order to run them against each other.

We also provide a trace file reader in the reader folder to run bigger traces from file.

## Usage

```cpp
LockFrame* lockFrame = new LockFrame();

// Add a detector to the frame
PWRDetector* pwrDetector = new PWRDetector();
lockFrame->set_detector(pwrDetector);

// Call events
lockFrame->write_event(1, 1, 1);
lockFrame->acquire_event(1, 2, 2);

// Get races
ASSERT_EQ(lockFrame->get_races().size(), 0);
```

## Available events

```cpp
void read_event(ThreadID, TracePosition, ResourceName);
void write_event(ThreadID, TracePosition, ResourceName);
void acquire_event(ThreadID, TracePosition, ResourceName);
void release_event(ThreadID, TracePosition, ResourceName);
void fork_event(ThreadID, TracePosition, ThreadID);
void join_event(ThreadID, TracePosition, ThreadID);
void notify_event(ThreadID, TracePosition, ResourceName);
void wait_event(ThreadID, TracePosition, ResourceName);
void get_races();
```

## Available detectors

* PWR (https://arxiv.org/pdf/2004.06969.pdf)
* UNDEAD (https://dl.acm.org/doi/pdf/10.5555/3155562.3155654)
* PWR+UNDEAD combined

## Create your own detector


Implement the `Detector` interface from detector.hpp

```cpp
class PWRDetector : public Detector {
    // ...
}
```