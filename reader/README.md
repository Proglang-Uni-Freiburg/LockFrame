# Reader

Reader enables you to read trace files in our or SpeedyGo's CSV format.

## Build

```
cmake -DCMAKE_BUILD_TYPE=Release . && cmake --build .
```

## Run

```
// Our format (with fork / join)
./reader PWR /home/jan/Dev/traces/papertests.log

// SpeedGo format (using signals instead of forks)
./reader PWR --speedygo /home/jan/Dev/traces/papertests.log
```

## Trace format

```
ThreadID,Event,ResourceInt
```

Event can be one of

* WR: Write
* RD: Read
* LK: Acquire
* UK: Release
* SIG: Fork (LF), Signal (SpeedyGo)
* WT: Join (LF), Signal Wait (SpeedyGo)
* NT: Notify
* NTWT: Notify Wait