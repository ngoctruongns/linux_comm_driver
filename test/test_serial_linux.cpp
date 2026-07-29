// Unit tests for SerialLinux.
//
// A pty pair stands in for the UART: the master end plays the role of the microcontroller
// (or USB-TTL adapter) and the slave end is opened by name, exactly like /dev/ttyUSB0.
#include "test_harness.hpp"

#include <poll.h>
#include <pty.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "serial_linux.hpp"

namespace
{

/// Owns a pty pair. `master()` is the far end; `name()` is what SerialLinux opens.
class PtyLink
{
public:
    PtyLink()
    {
        int slave = -1;
        char name[256] = {0};
        if (openpty(&master_, &slave, name, nullptr, nullptr) == 0) {
            ::close(slave);  // SerialLinux reopens it by name
            name_ = name;
        }
    }

    ~PtyLink()
    {
        closeMaster();
    }

    PtyLink(const PtyLink &) = delete;
    PtyLink &operator=(const PtyLink &) = delete;

    void closeMaster()
    {
        if (master_ != -1) {
            ::close(master_);
            master_ = -1;
        }
    }

    bool valid() const { return master_ != -1 && !name_.empty(); }
    int master() const { return master_; }
    const std::string &name() const { return name_; }

private:
    int master_{-1};
    std::string name_;
};

/// Open a driver on @p link with std::cerr logging turned off.
bool openQuiet(SerialLinux &port, const PtyLink &link, int baudrate = 115200)
{
    port.setLogEnabled(false);
    return port.open(link.name(), baudrate);
}

/// Read from the far end until @p expected bytes arrive or the attempts run out.
size_t readFromMaster(const PtyLink &link, uint8_t *buffer, size_t expected)
{
    size_t got = 0;
    for (int attempt = 0; attempt < 50 && got < expected; ++attempt) {
        struct pollfd pfd{link.master(), POLLIN, 0};
        if (::poll(&pfd, 1, 100) > 0) {
            const ssize_t n = ::read(link.master(), buffer + got, expected - got);
            if (n > 0) {
                got += static_cast<size_t>(n);
            }
        }
    }
    return got;
}

}  // namespace

TEST_CASE(baudrate_table)
{
    CHECK(SerialLinux::isBaudrateSupported(9600));
    CHECK(SerialLinux::isBaudrateSupported(115200));
    CHECK(SerialLinux::isBaudrateSupported(921600));
    CHECK(SerialLinux::isBaudrateSupported(2000000));
    CHECK(!SerialLinux::isBaudrateSupported(12345));
    CHECK(!SerialLinux::isBaudrateSupported(0));
}

TEST_CASE(default_constructed_is_closed)
{
    SerialLinux port;
    CHECK(!port.isOpen());
    CHECK(port.baudrate() == 0);
    CHECK(port.port().empty());
    CHECK(port.lastError().empty());

    // open() without a previous port must fail instead of touching a random device.
    port.setLogEnabled(false);
    CHECK(!port.open());
    CHECK(!port.lastError().empty());
}

TEST_CASE(open_failure_reports_reason)
{
    SerialLinux port;
    port.setLogEnabled(false);
    CHECK(!port.open("/dev/definitely_not_a_serial_port", 115200));
    CHECK(!port.isOpen());
    CHECK(port.lastError().find("definitely_not_a_serial_port") != std::string::npos);
}

TEST_CASE(unsupported_baudrate_falls_back_to_9600)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    port.setLogEnabled(false);
    // Documented behaviour: the port still opens, but the failure is reported.
    CHECK(port.open(link.name(), 12345));
    CHECK(port.isOpen());
    CHECK(port.lastError().find("unsupported baud rate") != std::string::npos);
}

TEST_CASE(open_close_reopen)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));
    CHECK(port.isOpen());
    CHECK(port.port() == link.name());
    CHECK_EQ(port.baudrate(), 115200);

    port.close();
    CHECK(!port.isOpen());
    port.close();  // closing twice must be harmless
    CHECK(!port.isOpen());

    CHECK(port.open());  // remembers port and baudrate
    CHECK(port.isOpen());
    CHECK(port.port() == link.name());
}

TEST_CASE(constructor_opens_immediately)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port(link.name(), 115200);  // legacy constructor
    CHECK(port.isOpen());
    CHECK_EQ(port.baudrate(), 115200);
}

TEST_CASE(idle_read_returns_zero_not_error)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    std::array<uint8_t, 64> buffer{};

    const auto start = std::chrono::steady_clock::now();
    CHECK_EQ(port.readData(buffer.data(), buffer.size(), 120), 0);
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    CHECK(waited.count() >= 100);  // it blocked instead of spinning

    // Legacy overload: "no data" is 0, not -1.
    CHECK_EQ(port.readData(buffer.data(), buffer.size()), 0);
}

TEST_CASE(read_on_closed_port_is_error)
{
    SerialLinux port;
    port.setLogEnabled(false);
    std::array<uint8_t, 8> buffer{};
    CHECK_EQ(port.readData(buffer.data(), buffer.size(), 10), -1);
    CHECK(!port.writeData(buffer.data(), buffer.size()));
}

TEST_CASE(round_trip_bytes)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    // Far end -> driver
    const std::string incoming = "\xAA\x01\x02\xDD from the mcu";
    REQUIRE(::write(link.master(), incoming.data(), incoming.size()) ==
            static_cast<ssize_t>(incoming.size()));

    std::array<uint8_t, 128> buffer{};
    size_t got = 0;
    for (int attempt = 0; attempt < 50 && got < incoming.size(); ++attempt) {
        const int n = port.readData(buffer.data() + got, buffer.size() - got, 100);
        REQUIRE(n >= 0);
        got += static_cast<size_t>(n);
    }
    CHECK_EQ(got, incoming.size());
    CHECK(std::memcmp(buffer.data(), incoming.data(), incoming.size()) == 0);

    // Driver -> far end, byte for byte (including 0x00 and high bytes)
    const std::array<uint8_t, 6> outgoing{0xAA, 0x00, 0x7D, 0xFF, 0x0A, 0xDD};
    CHECK(port.writeData(outgoing.data(), outgoing.size()));
    CHECK(port.drain());

    std::array<uint8_t, 16> echo{};
    CHECK_EQ(readFromMaster(link, echo.data(), outgoing.size()), outgoing.size());
    CHECK(std::memcmp(echo.data(), outgoing.data(), outgoing.size()) == 0);
}

TEST_CASE(large_write_is_never_partial)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    // Bigger than the pty buffer, so write() hits EAGAIN and must wait for POLLOUT.
    const std::vector<uint8_t> payload(16384, 0x41);
    std::atomic<size_t> drained{0};
    std::thread drainer([&] {
        std::array<uint8_t, 1024> buffer{};
        while (drained < payload.size()) {
            struct pollfd pfd{link.master(), POLLIN, 0};
            if (::poll(&pfd, 1, 500) <= 0) {
                break;
            }
            const ssize_t n = ::read(link.master(), buffer.data(), buffer.size());
            if (n <= 0) {
                break;
            }
            drained += static_cast<size_t>(n);
        }
    });

    CHECK(port.writeData(payload.data(), payload.size()));
    drainer.join();
    CHECK_EQ(drained.load(), payload.size());
}

TEST_CASE(concurrent_write_while_reading)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    // Two writer threads plus a reader: writeData() must serialise so that no frame
    // interleaves with another. Each writer sends its own repeated byte pattern.
    std::atomic<bool> stop{false};
    std::atomic<int> write_failures{0};
    auto writer = [&](uint8_t value) {
        const std::vector<uint8_t> chunk(32, value);
        for (int i = 0; i < 50; ++i) {
            if (!port.writeData(chunk.data(), chunk.size())) {
                ++write_failures;
            }
        }
    };

    std::thread drainer([&] {
        std::array<uint8_t, 256> buffer{};
        while (!stop) {
            struct pollfd pfd{link.master(), POLLIN, 0};
            if (::poll(&pfd, 1, 50) > 0) {
                if (::read(link.master(), buffer.data(), buffer.size()) <= 0) {
                    break;
                }
            }
        }
    });

    std::thread a(writer, 0x11);
    std::thread b(writer, 0x22);
    a.join();
    b.join();
    stop = true;
    drainer.join();

    CHECK_EQ(write_failures.load(), 0);
    CHECK(port.isOpen());
}

TEST_CASE(flush_discards_pending_input)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    const std::string junk = "stale bytes";
    REQUIRE(::write(link.master(), junk.data(), junk.size()) ==
            static_cast<ssize_t>(junk.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let it reach the tty

    CHECK(port.flushInput());

    std::array<uint8_t, 64> buffer{};
    CHECK_EQ(port.readData(buffer.data(), buffer.size(), 100), 0);
}

TEST_CASE(disconnect_is_reported_as_error)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux port;
    REQUIRE(openQuiet(port, link));

    link.closeMaster();  // cable unplugged / firmware end closed

    std::array<uint8_t, 64> buffer{};
    int rc = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        rc = port.readData(buffer.data(), buffer.size(), 100);
        if (rc < 0) {
            break;
        }
    }
    CHECK(rc < 0);
    CHECK(!port.lastError().empty());
}

TEST_CASE(move_transfers_ownership)
{
    PtyLink link;
    REQUIRE(link.valid());

    SerialLinux source;
    REQUIRE(openQuiet(source, link));

    SerialLinux moved(std::move(source));
    CHECK(moved.isOpen());
    CHECK(!source.isOpen());  // must not close the descriptor `moved` now owns
    CHECK(moved.port() == link.name());

    // The moved-to object still works on the real device.
    const std::array<uint8_t, 3> data{1, 2, 3};
    CHECK(moved.writeData(data.data(), data.size()));
    std::array<uint8_t, 3> echo{};
    CHECK_EQ(readFromMaster(link, echo.data(), data.size()), data.size());
}

TEST_CASE(move_assignment_closes_previous_port)
{
    PtyLink first;
    PtyLink second;
    REQUIRE(first.valid());
    REQUIRE(second.valid());

    SerialLinux target;
    REQUIRE(openQuiet(target, first));

    SerialLinux other;
    REQUIRE(openQuiet(other, second));

    target = std::move(other);
    CHECK(target.isOpen());
    CHECK(target.port() == second.name());
    CHECK(!other.isOpen());
}

TEST_MAIN()
