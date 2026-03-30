#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdint>
#include <string>
#include <iostream>

class SerialLinux
{
public:
    SerialLinux(const std::string &port, int baudrate);
    ~SerialLinux();

    // Delete copy constructor and assignment operator
    SerialLinux(const SerialLinux &) = delete;
    SerialLinux &operator=(const SerialLinux &) = delete;

    // Default move constructor and assignment operator
    SerialLinux(SerialLinux &&) = default;
    SerialLinux &operator=(SerialLinux &&) = default;

    bool isOpen() const
    {
        return fd_ != -1;
    }
    bool writeData(const uint8_t *data, size_t size);
    int readData(uint8_t *buffer, size_t size);

private:
    std::string port_;
    int baudrate_;
    int fd_;
    speed_t getBaudrateConstant(int baudrate);
};
