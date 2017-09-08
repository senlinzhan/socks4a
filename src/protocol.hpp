#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <iostream>
#include <string>

#include <event2/buffer.h>

class ProtocolInfo
{
    friend std::ostream &operator<<(std::ostream &os, const ProtocolInfo &info);
    
public:
    ProtocolInfo(evbuffer *input);

    enum class Status   {success, error, incomplete};    
    enum class Protocol {socks4, socks4a};

    Status status() const;

    Protocol protocol() const;

    const std::string &error() const;

    void responseSuccess(evbuffer *output);

    unsigned short port() const;

    unsigned int ip() const;

    const std::string &domain() const;
    
private:
    int             version_;
    int             command_;
    unsigned short  port_;
    unsigned int    ip_;
    std::string     ipString_;
    std::string     domain_;

    Protocol        protocol_;    
    Status          status_;    
    std::string     error_;    
};

std::ostream &operator<<(std::ostream &os, const ProtocolInfo &info);

#endif /* PROTOCOL_H */
