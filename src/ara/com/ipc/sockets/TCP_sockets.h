/**
 * @file TCP_sockets.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-19
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef TCP_SCOKETS_H
#define TCP_SCOKETS_H

#include <cstdint>
#include <iostream>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#include "sockets.h"



class TcpSocket : public Sockets
{
public:
    // Constructor.
    TcpSocket(std::string const &host, uint16_t port) : host_(host), port_(port){}
    
    // Destructor.
    ~TcpSocket() override =default;
    
    // Open the TCP socket and connect to the specified host and port.
    bool Open() override{
        // Create a socket file descriptor.
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Error creating TCP socket." << std::endl;
            return false;
        }
        
        // Get the network address and port number of the remote host.
        struct addrinfo hints;
        struct addrinfo *server_info;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &server_info) != 0) {
            std::cerr << "Error resolving host." << std::endl;
            return false;
        }
        
        // Connect to the remote host using the network address and port number.
        if (connect(socket_fd_, server_info->ai_addr, server_info->ai_addrlen) < 0) {
            std::cerr << "Error connecting to remote host." << std::endl;
            return false;
        }
        
        // Free the server info structure.
        freeaddrinfo(server_info);
        
        return true;
    }
    
    // Close the TCP socket.
    void Close() override{
        if (socket_fd_ != 0) {
            close(socket_fd_);
            socket_fd_ = 0;
        }
    }
    
    // Send data over the TCP socket.
    bool Send(const std::string& message, std::string const& host = "", uint16_t port = 0) const override{
        long bytes_sent = send(socket_fd_, message.c_str(), message.size(), 0);
        if (bytes_sent < 0) {
            std::cerr << "Error sending TCP message." << std::endl;
            return false;
        }
        return true;
    }
    
    // Receive data from the TCP socket.
    bool Receive(std::string& message, std::string& , uint16_t& ) const override{
        std::string buffer(1024, 0);
        long bytes_received = recv(socket_fd_, &buffer[0], buffer.size(), 0);
        if (bytes_received < 0) {
            std::cerr << "Error receiving TCP message." << std::endl;
            return false;
        }
        
        // Convert the buffer to a string and pass it back to the caller.
        message= buffer.substr(0, bytes_received);
        return true;
    }
    
private:
    std::string host_;
    uint16_t port_;
    int socket_fd_{0};
};







#endif // TCP_SCOKETS_H