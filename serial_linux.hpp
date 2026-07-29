#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdint>
#include <mutex>
#include <string>

/**
 * @brief Blocking-capable serial port driver for Linux (raw mode, 8N1, no flow control).
 *
 * The port is opened non-blocking and reads go through poll(), so a caller can either
 * poll cheaply (timeout 0) or block with a timeout without burning CPU.
 *
 * Thread safety: writeData() is serialised internally, so one thread may read while
 * another writes. Concurrent readers are not supported.
 */
class SerialLinux
{
public:
    SerialLinux() = default;

    /// Construct and open immediately. Check isOpen() / lastError() afterwards.
    SerialLinux(const std::string &port, int baudrate);
    ~SerialLinux();

    // Delete copy constructor and assignment operator
    SerialLinux(const SerialLinux &) = delete;
    SerialLinux &operator=(const SerialLinux &) = delete;

    // Move transfers ownership of the file descriptor
    SerialLinux(SerialLinux &&other) noexcept;
    SerialLinux &operator=(SerialLinux &&other) noexcept;

    /// Open @p port at @p baudrate, closing any port currently held.
    bool open(const std::string &port, int baudrate);

    /// Reopen the port/baudrate given earlier. Useful for reconnect loops.
    bool open();

    void close();

    bool isOpen() const;

    /// Write @p size bytes, retrying until everything is out or the port fails.
    bool writeData(const uint8_t *data, size_t size);

    /// Non-blocking read: returns bytes read, 0 if no data is available, -1 on error.
    int readData(uint8_t *buffer, size_t size);

    /**
     * @brief Read with a timeout.
     * @param timeout_ms >0 wait up to this long, 0 return immediately, <0 wait forever.
     * @return bytes read (>0), 0 on timeout, -1 on error (the port should be reopened).
     */
    int readData(uint8_t *buffer, size_t size, int timeout_ms);

    /// Discard data already received but not read.
    bool flushInput();

    /// Discard both pending input and pending output.
    bool flushIO();

    /// Block until everything written has been transmitted.
    bool drain();

    /// Reason for the most recent failure ("" if none). Returned by value: the
    /// internal string is mutex protected and may change under a concurrent reader.
    std::string lastError() const;

    std::string port() const;
    int baudrate() const;

    /// Also print failures to std::cerr. Default true (previous behaviour);
    /// set false when the application logs lastError() itself (e.g. ROS 2).
    void setLogEnabled(bool enabled);

    static bool isBaudrateSupported(int baudrate);

private:
    bool configure(int fd);
    void setError(const std::string &message);
    /// Descriptor (or -1) plus an optional copy of the port name, taken under one
    /// lock so I/O never touches the shared state while it runs.
    int descriptor(std::string *port_out = nullptr) const;
    static speed_t getBaudrateConstant(int baudrate);

    std::string port_;
    int baudrate_{0};
    int fd_{-1};
    bool log_enabled_{true};
    std::string last_error_;
    mutable std::mutex state_mutex_;
    std::mutex write_mutex_;
};
