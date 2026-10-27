# NUS send regressions

Run on Linux with Python 3 and a C compiler supporting AddressSanitizer and
UndefinedBehaviorSanitizer:

```sh
python3 tests/nus_send/run.py
python3 tests/nus_send/run.py --source-ref 0bc7edb864e675f6914583edba26d5d6d0a45abf --expect-failure
```

The harness compiles the production send functions with mocked semaphore and
Bluetooth APIs. It checks an ordinary StateSync packet's asynchronous buffer
lifetime, timeout behavior, immediate send errors, completion slot release and
oversized-packet rejection. Debug-mode checks use the production watcher macros
and verify that both watched and unwatched sends retain `K_FOREVER` waits.
The unchanged-source command is a negative control: each release case must fail
an assertion or sanitizer check, while the debug compatibility checks still pass.

The client mock retains the submitted pointer, as permitted by the NUS API.
This demonstrates the API lifetime requirement, not observed corruption on
released hardware. The semaphore mock injects timeouts; it does not establish
their frequency on hardware. Disconnect/reconnect callback ordering and actual
wireless typing still require device testing.
