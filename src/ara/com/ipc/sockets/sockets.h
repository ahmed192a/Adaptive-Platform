/**
 * @file sockets.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-19
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#ifndef SOCKETS_H
#define SOCKETS_H

#include <string>
#include <iostream>

class Sockets {
public:
    virtual ~Sockets() = 0;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual bool Send(const std::string& message , std::string const&host = "", uint16_t port = 0)const = 0;
    virtual bool Receive(std::string& message, std::string& host , uint16_t& port )const = 0;
};



#endif // SOCKETS_H