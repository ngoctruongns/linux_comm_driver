#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdint>
#include <string>
#include <iostream>

class SerialLinux {
    public:
        SerialLinux(const std::string& port, int baudrate);
        ~SerialLinux();

        bool isOpen() const { return fd_ != -1; }
        bool writeData(const uint8_t* data, size_t size);
        int readData(uint8_t* buffer, size_t size);

    private:
        std::string port_;
        int baudrate_;
        int fd_;
        speed_t getBaudrateConstant(int baudrate);
};