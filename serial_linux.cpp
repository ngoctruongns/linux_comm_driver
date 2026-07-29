#include "serial_linux.hpp"

#include <poll.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>

SerialLinux::SerialLinux(const std::string &port, int baudrate)
{
    open(port, baudrate);
}

SerialLinux::~SerialLinux()
{
    close();
}

SerialLinux::SerialLinux(SerialLinux &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.state_mutex_);
    port_ = std::move(other.port_);
    baudrate_ = other.baudrate_;
    fd_ = other.fd_;
    log_enabled_ = other.log_enabled_;
    last_error_ = std::move(other.last_error_);
    other.fd_ = -1;  // the moved-from object must not close our descriptor
}

SerialLinux &SerialLinux::operator=(SerialLinux &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    close();
    std::lock_guard<std::mutex> lock_this(state_mutex_);
    std::lock_guard<std::mutex> lock_other(other.state_mutex_);
    port_ = std::move(other.port_);
    baudrate_ = other.baudrate_;
    fd_ = other.fd_;
    log_enabled_ = other.log_enabled_;
    last_error_ = std::move(other.last_error_);
    other.fd_ = -1;
    return *this;
}

void SerialLinux::setError(const std::string &message)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = message;
    }
    if (log_enabled_) {
        std::cerr << "SerialLinux: " << message << "\n";
    }
}

int SerialLinux::descriptor(std::string *port_out) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (port_out != nullptr) {
        *port_out = port_;
    }
    return fd_;
}

speed_t SerialLinux::getBaudrateConstant(int baudrate)
{
    switch (baudrate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
        case 1000000: return B1000000;
        case 1152000: return B1152000;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        default: return 0; // Unsupported baud rate
    }
}

bool SerialLinux::isBaudrateSupported(int baudrate)
{
    return getBaudrateConstant(baudrate) != 0;
}

bool SerialLinux::configure(int fd)
{
    struct termios options {};
    if (tcgetattr(fd, &options) != 0) {
        setError("tcgetattr(" + port_ + ") failed: " + std::strerror(errno));
        return false;
    }

    speed_t baudrate_const = getBaudrateConstant(baudrate_);
    if (baudrate_const == 0) {
        setError("unsupported baud rate " + std::to_string(baudrate_) + ", using 9600");
        baudrate_const = B9600;
    }

    cfmakeraw(&options);                    // Set raw mode
    cfsetispeed(&options, baudrate_const);
    cfsetospeed(&options, baudrate_const);

    options.c_cflag |= (CLOCAL | CREAD);    // Enable receiver, ignore modem control lines
    options.c_cflag &= ~CSIZE;              // Clear current data size setting
    options.c_cflag |= CS8;                 // 8 data bits
    options.c_cflag &= ~PARENB;             // No parity
    options.c_cflag &= ~CSTOPB;             // 1 stop bit
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;            // No hardware flow control
#endif
    options.c_iflag &= ~(IXON | IXOFF | IXANY);  // No software flow control

    // Reads are driven by poll(), so never let the driver block on its own.
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        setError("tcsetattr(" + port_ + ") failed: " + std::strerror(errno));
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    return true;
}

bool SerialLinux::open(const std::string &port, int baudrate)
{
    close();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        port_ = port;
        baudrate_ = baudrate;
        last_error_.clear();
    }

    const int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        setError("failed to open port " + port + ": " + std::strerror(errno));
        return false;
    }

    if (!configure(fd)) {
        ::close(fd);
        return false;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    fd_ = fd;
    return true;
}

bool SerialLinux::open()
{
    std::string port;
    int baudrate;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        port = port_;
        baudrate = baudrate_;
    }
    if (port.empty()) {
        setError("no port configured; call open(port, baudrate) first");
        return false;
    }
    return open(port, baudrate);
}

void SerialLinux::close()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialLinux::isOpen() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return fd_ != -1;
}

std::string SerialLinux::lastError() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return last_error_;
}

std::string SerialLinux::port() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return port_;
}

int SerialLinux::baudrate() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return baudrate_;
}

void SerialLinux::setLogEnabled(bool enabled)
{
    log_enabled_ = enabled;
}

bool SerialLinux::writeData(const uint8_t *data, size_t size)
{
    std::string port;
    const int fd = descriptor(&port);
    if (fd == -1) {
        setError("port not open for writing");
        return false;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    size_t written = 0;
    while (written < size) {
        const ssize_t bytes_written = ::write(fd, data + written, size - written);
        if (bytes_written > 0) {
            written += static_cast<size_t>(bytes_written);
            continue;
        }
        if (bytes_written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            // Kernel buffer full: wait for room instead of reporting a partial write.
            struct pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            if (::poll(&pfd, 1, 100) <= 0) {
                setError("write timed out on " + port);
                return false;
            }
            continue;
        }
        setError("write failed on " + port + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

int SerialLinux::readData(uint8_t *buffer, size_t size)
{
    return readData(buffer, size, 0);
}

int SerialLinux::readData(uint8_t *buffer, size_t size, int timeout_ms)
{
    std::string port;
    const int fd = descriptor(&port);
    if (fd == -1) {
        setError("port not open for reading");
        return -1;
    }

    struct pollfd pfd {};
    pfd.fd = fd;
    pfd.events = POLLIN;

    const int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready == 0) {
        return 0;  // no data within the timeout
    }
    if (ready == -1) {
        if (errno == EINTR) {
            return 0;
        }
        setError("poll failed on " + port + ": " + std::strerror(errno));
        return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        setError("port " + port + " disconnected");
        return -1;
    }

    const ssize_t bytes_read = ::read(fd, buffer, size);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;  // spurious wakeup, not an error
        }
        setError("read failed on " + port + ": " + std::strerror(errno));
        return -1;
    }
    return static_cast<int>(bytes_read);
}

bool SerialLinux::flushInput()
{
    const int fd = descriptor();
    if (fd == -1) {
        return false;
    }
    return tcflush(fd, TCIFLUSH) == 0;
}

bool SerialLinux::flushIO()
{
    const int fd = descriptor();
    if (fd == -1) {
        return false;
    }
    return tcflush(fd, TCIOFLUSH) == 0;
}

bool SerialLinux::drain()
{
    const int fd = descriptor();
    if (fd == -1) {
        return false;
    }
    return tcdrain(fd) == 0;
}
