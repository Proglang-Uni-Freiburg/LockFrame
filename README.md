## Usage

```cpp
LockFrame* lockFrame = new LockFrame();

// Add a detector to the frame
PWRDetector* pwrDetector = new PWRDetector();
lockFrame->set_detector(pwrDetector);

// Call events
lockFrame->write_event(1, 1, std::string("x"));
lockFrame->aquire_event(1, 2, std::string("y"));

// Get races
ASSERT_EQ(lockFrame->get_races().size(), 1);
```

## Available events

```cpp
void read_event(ThreadID, TracePosition, ResourceName);
void write_event(ThreadID, TracePosition, ResourceName);
void aquire_event(ThreadID, TracePosition, ResourceName);
void release_event(ThreadID, TracePosition, ResourceName);
void fork_event(ThreadID, TracePosition, ResourceName);
void join_event(ThreadID, TracePosition, ResourceName);
```

## Available detectors

* PWR (https://arxiv.org/pdf/2004.06969.pdf)

## Create your own detector


Implement the `Detector` interface from detector.hpp

```cpp
class PWRDetector : public Detector {
    // ...
}
```