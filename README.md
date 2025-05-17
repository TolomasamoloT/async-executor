# async-executor
A Tokio-inspired asynchronous executor in C.

Works like this:

executor_spawn                     COMPLETED / FAILURE
     │                                       ▲
     ▼               executor calls          │
   PENDING  ───► fut->progress(fut, waker) ──+
  (enqued)                                   │
     ▲                                       │
     │                                       ▼
     └─── smbd (ex. mio_poll) calls ◄──── PENDING
              waker_wake(waker)       (waker waits)

## Helpful Commands
- Preparing the build: `mkdir build; cd build; cmake ..`
- Building tests: `cd build/tests; cmake --build ..`
- Running all tests: `cd build/tests; ctest --output-on-failure`
- Running a single test: `./<test_name>`


<pre> ``` executor_spawn COMPLETED / FAILURE │ ▲ ▼ executor calls │ PENDING ───► fut->progress(fut, waker) ──+ (enqued) │ ▲ │ │ ▼ └─── smbd (ex. mio_poll) calls ◄──── PENDING waker_wake(waker) (waker waits) ``` </pre>
