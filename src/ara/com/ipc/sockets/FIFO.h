/**
 * @file FIFO.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-19
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef FIFO_H
#define FIFO_H

#include <cstdint>
#include <fcntl.h>
#include <string>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

#include "sockets.h"

class FIFO : public Sockets {
public:
    // Constructor.
    explicit FIFO(std::string const& name) : name_(name) {}
    
    // Destructor.
    ~FIFO() override = default;
    
    // Open the FIFO.
    bool Open() override {
        // Create the FIFO.
        if (mkfifo(name_.c_str(), 0666) < 0) {
            std::cerr << "Error creating FIFO." << std::endl;
            return false;
        }
        
        // Open the FIFO for reading and writing.
        fifo_fd_ = open(name_.c_str(), O_RDWR);
        if (fifo_fd_ < 0) {
            std::cerr << "Error opening FIFO." << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Close the FIFO.
    void Close() override {
        close(fifo_fd_);
    }
    
    // Send a message to the FIFO.
    bool Send(const std::string& message , std::string const&host = "", uint16_t port = 0)const override {
        // Write the message to the FIFO.
        if (write(fifo_fd_, message.c_str(), message.length()) < 0) {
            std::cerr << "Error writing to FIFO." << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Receive a message from the FIFO.
    bool Receive(std::string& message, std::string& host , uint16_t& port )const override {
        // Read the message from the FIFO.
        std::string buffer (1024, 0);
        long bytes_read = read(fifo_fd_, &buffer[0], buffer.length());
        if ( bytes_read< 0) {
            std::cerr << "Error reading from FIFO." << std::endl;
            return false;
        }
        
        message= buffer.substr(0, bytes_read);
        
        return true;
    }

private:
    std::string name_;
    int fifo_fd_;
};

#endif // FIFO_H