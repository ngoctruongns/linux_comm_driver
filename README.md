# linux_comm_driver

Reusable serial port driver for Linux (`SerialLinux`): raw mode, 8N1, no flow control.
The port is opened non-blocking and every read goes through `poll()`, so callers can
either poll cheaply or block with a timeout without burning CPU.

## 1) API

```cpp
#include "serial_linux.hpp"

SerialLinux port;                        // or: SerialLinux port("/dev/ttyUSB0", 115200);
port.setLogEnabled(false);               // silence std::cerr, read lastError() instead

if (!port.open("/dev/ttyUSB0", 115200)) {
    std::cerr << port.lastError() << "\n";
    return 1;
}

uint8_t buffer[256];
int n = port.readData(buffer, sizeof(buffer), 100);   // 100 ms timeout
// n > 0 : bytes read     n == 0 : timeout / no data     n < 0 : error, reopen the port

port.writeData(data, size);              // retries until everything is out
port.close();
port.open();                             // reopen the same port/baudrate
```

| Method | Notes |
| --- | --- |
| `SerialLinux()` | Does not open anything; call `open(port, baudrate)`. |
| `SerialLinux(port, baudrate)` | Opens immediately; check `isOpen()` / `lastError()`. |
| `open(port, baudrate)` | Closes any port currently held, then opens. |
| `open()` | Reopens the last port/baudrate — for reconnect loops. |
| `close()` / `isOpen()` | |
| `writeData(data, size)` | Waits for `POLLOUT` on a full kernel buffer, so a large buffer is never written partially. |
| `readData(buf, size)` | Non-blocking: returns `0` when no data is available, `-1` only on a real error. |
| `readData(buf, size, timeout_ms)` | `>0` wait up to that long, `0` return at once, `<0` wait forever. Returns `0` on timeout. |
| `flushInput()` / `flushIO()` / `drain()` | Discard input / discard both directions / block until output is transmitted. |
| `lastError()` | Reason for the most recent failure; returned by value. |
| `port()` / `baudrate()` | Current settings. |
| `setLogEnabled(bool)` | Print failures to `std::cerr` as well. Default `true`. |
| `isBaudrateSupported(int)` | Static; validate before opening. |

Supported baud rates: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 500000,
576000, 921600, 1000000, 1152000, 1500000, 2000000.

**Threading.** `writeData()` is serialised internally, so one thread may read while
another writes — the usual layout for a receive loop plus command callbacks.
Concurrent *readers* are not supported. The object is movable (the descriptor is
transferred, not duplicated) and not copyable.

**Disconnects.** If the device disappears (USB adapter unplugged, far end closed),
`readData()` returns `-1` and `lastError()` says which port dropped. Recover with
`close()` then `open()`.

## 2) Build

Requires C++17 and a POSIX system (`termios`, `poll`). Either add the repository to a
parent project, which gives you the `linux_comm_driver` target:

```cmake
add_subdirectory(third_party/linux_comm_driver)
target_link_libraries(my_app PRIVATE linux_comm_driver)
```

or just compile the two sources directly:

```cmake
add_library(linux_comm_driver STATIC linux_comm_driver/serial_linux.cpp)
target_include_directories(linux_comm_driver PUBLIC linux_comm_driver)
target_compile_features(linux_comm_driver PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(linux_comm_driver PUBLIC Threads::Threads)
```

## 3) Tests

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

[test/test_serial_linux.cpp](test/test_serial_linux.cpp) drives the driver through a
**pty pair**: the master end plays the microcontroller, the slave end is opened by name
exactly like `/dev/ttyUSB0`, so no hardware is needed. Covered: the baudrate table,
open failures and their messages, close/reopen, read timeouts (including "no data is 0,
not -1"), byte-exact round trips, a write larger than the kernel buffer, two writer
threads against one reader, `flushInput()`, disconnect detection, and move semantics.

The tests use a small built-in harness ([test/test_harness.hpp](test/test_harness.hpp))
instead of GoogleTest, so they build with nothing but a compiler — including on a board
where gtest is not installed. Add a case with:

```cpp
TEST_CASE(my_case)
{
    CHECK(port.isOpen());
    CHECK_EQ(port.baudrate(), 115200);
    REQUIRE(link.valid());   // gives up on this case only
}
```

Run one case by name: `./build/test_serial_linux move_`.

When the repository is used inside a parent project (e.g. a ROS 2 package), the suite is
picked up automatically whenever `BUILD_TESTING` is on, so `colcon test` runs it too.

## 4) Setup UART port

Add dependencies:

    sudo apt install libudev-dev

Install the udev rules:

    cd rules
    ./create_udev_rules.sh

This creates stable symlinks so the port name does not depend on enumeration order:

| Rule | Symlink | Device |
| --- | --- | --- |
| `70-raspberry.rules` | `/dev/gpio_uart` | Raspberry Pi GPIO UART (`ttyAMA0`) |
| `70-arduino.rules` | `/dev/arduino_serial` | Arduino Uno (VID `2341`, PID `0043`) |
| `70-arduino.rules` | `/dev/ch340_uart` | CH340 USB-TTL adapter (VID `1a86`, PID `7523`) |

Remove them again with `./delete_udev_rules.sh`.

The GPIO UART symlink is `GROUP="dialout"`, so add your user to that group once:

    sudo usermod -aG dialout $USER      # re-login afterwards
