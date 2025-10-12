#include "serial_linux.hpp"

SerialLinux::SerialLinux(const std::string &port, int baudrate)
    : port_(port), baudrate_(baudrate), fd_(-1)
{
    fd_ = open(port_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ == -1) {
        std::cerr << "Failed to open port: " << port_ << "\n";
        return;
    }

    struct termios options;
    tcgetattr(fd_, &options);

    // Set baud rate
    speed_t baudrate_const = getBaudrateConstant(baudrate_);
    if (baudrate_const == 0) {
        std::cerr << "Unsupported baud rate: " << baudrate_ << "\n";
        std::cerr << "Used default 9600.\n";
        baudrate_const = B9600;
    }

    cfsetispeed(&options, baudrate_const);
    cfsetospeed(&options, baudrate_const);

    cfmakeraw(&options);                    // Set raw mode
    options.c_cflag |= (CLOCAL | CREAD);    // Enable receiver, ignore modem control lines
    options.c_cflag &= ~CSIZE;              // Clear current data size setting
    options.c_cflag |= CS8;                 // 8 data bits
    options.c_cflag &= ~PARENB;             // No parity
    options.c_cflag &= ~CSTOPB;             // 1 stop bit
    options.c_cflag &= ~CRTSCTS;            // No flow control

    tcflush(fd_, TCIFLUSH); //
    tcsetattr(fd_, TCSANOW, &options);
}

SerialLinux::~SerialLinux() {
    if (isOpen()) {
        close(fd_);
    }
}

speed_t SerialLinux::getBaudrateConstant(int baudrate) {
    switch (baudrate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return 0; // Unsupported baud rate
    }
}

bool SerialLinux::writeData(const uint8_t* data, size_t size) {
    if (!isOpen()) {
        std::cerr << "Port not open for writing.\n";
        return false;
    }
    ssize_t bytes_written = write(fd_, data, size);
    return bytes_written == static_cast<ssize_t>(size);
}

int SerialLinux::readData(uint8_t* buffer, size_t size) {
    if (!isOpen()) {
        std::cerr << "Port not open for reading.\n";
        return -1;
    }
    ssize_t bytes_read = read(fd_, buffer, size);
    return bytes_read;  // Returns number of bytes read or -1 on error
}