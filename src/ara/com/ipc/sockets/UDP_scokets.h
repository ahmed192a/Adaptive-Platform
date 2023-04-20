/**
 * @file UDP_scokets.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-19
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#ifndef UDP_SCOKETS_H
#define UDP_SCOKETS_H

#include <cstdint>
#include <iostream>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "sockets.h"


class UdpSocket : public Sockets 
{
public:
    // Constructor.
    UdpSocket(std::string const& host,  uint16_t port)  : host_(host), port_(port){}
    
    // Destructor.
    ~UdpSocket() override =default;


    
    // Open the UDP socket.
    bool Open() override {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Error opening UDP socket." << std::endl;
            return false;
        }
        
        // Zero out the server address structure and set the address family and port number.
        std::memset(&server_address_, 0, sizeof(server_address_));
        server_address_.sin_family = AF_INET;
        server_address_.sin_port = htons(port_);
        server_address_.sin_addr.s_addr = htonl(INADDR_ANY);
        
        // Bind the socket to the server address.
        if (bind(socket_fd_, (struct sockaddr*)&server_address_, sizeof(server_address_)) < 0) {
            std::cerr << "Error binding UDP socket." << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Close the UDP socket.
    void Close() override{
        if (socket_fd_ != 0) {
            close(socket_fd_);
            socket_fd_ = 0;
        }
    }
    
    // Send data over the UDP socket.
    bool Send(const std::string& message, const std::string& host = "", uint16_t port = 0) const override{
        
        // Zero out the client address structure and set the address family and port number.
        struct sockaddr_in client_address;
        std::memset(&client_address, 0, sizeof(client_address));
        client_address.sin_family = AF_INET;
        client_address.sin_port = htons(port);
        
        // Convert the host string to a network address structure and set it as the client address.
        if (inet_pton(AF_INET, host.c_str(), &client_address.sin_addr) <= 0) {
            std::cerr << "Invalid host." << std::endl;
            return false;
        }
        
        // Send the message over the UDP socket to the client address.
        long bytes_sent = sendto(socket_fd_, message.c_str(), message.size(), 0,
                                (struct sockaddr*)&client_address, sizeof(client_address));
        if (bytes_sent < 0) {
            std::cerr << "Error sending UDP message." << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Receive data from the UDP socket.
    bool Receive(std::string& message, std::string& host, uint16_t& port) const override{
        // Zero out the client address structure.
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        std::memset(&client_address, 0, client_address_length);
        
        // Receive the message over the UDP socket.
        std::string buffer(1024, 0);
        long bytes_received = recvfrom(socket_fd_, &buffer[0], buffer.size(), 0,
                                      (struct sockaddr*)&client_address, &client_address_length);
        if (bytes_received < 0) {
            std::cerr << "Error receiving UDP message." << std::endl;
            return false;
        }
        
        // Convert the network address structure to a host string and port number.
        std::string host_buffer(INET_ADDRSTRLEN, 0);
        inet_ntop(AF_INET, &client_address.sin_addr, &host_buffer[0], (socklen_t)host_buffer.size());
        host = host_buffer;
        port = ntohs(client_address.sin_port);
        
        // Convert the buffer to a string and pass it back to the caller.
        message= buffer.substr(0, bytes_received);
        return true;
    }
    
private:
    std::string host_;
    uint16_t port_;
    int socket_fd_{0};
    struct sockaddr_in server_address_;
};

#endif // UDP_SCOKETS_H