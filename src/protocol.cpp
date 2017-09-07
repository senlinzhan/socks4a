#include "protocol.hpp"

#include <string.h>

ProtocolInfo::ProtocolInfo(evbuffer *input)
{
    size_t len = evbuffer_get_length(input);
        
    if (len < 9)
    {
        status_ = Status::incomplete;
        return;            
    }

    std::string buf(len, '\0');    
    evbuffer_copyout(input, (void *)buf.data(), len);
        
    auto pos = buf.find('\0', 8);    
    if (pos == std::string::npos)
    {
        status_ = Status::incomplete;
        return;            
    }
    
    version_ = static_cast<int>(buf[0]);
    command_ = static_cast<int>(buf[1]);

    if (version_ != 4)
    {
        status_ = Status::error;
        error_ = std::string("invalid version: ") + std::to_string(version_);
        return;            
    }

    if (command_ != 1)
    {
        status_ = Status::error;
        error_ = std::string("invalid command: ") + std::to_string(command_);
        return;            
    }
        
    port_ = ntohs(*reinterpret_cast<const unsigned short *>(&buf[2]));
    ip_ = ntohl(*reinterpret_cast<const unsigned int *>(&buf[4]));    

    ipString_.resize(INET_ADDRSTRLEN);
    if (inet_ntop(AF_INET, &(ip_), &(ipString_[0]), INET_ADDRSTRLEN) == nullptr)
    {
        status_ = Status::error;
        error_ = std::string("convert ip address ") + std::to_string(ip_) + " to string error: " + strerror(errno);
        return;            
    }
    if (ipString_.find("0.0.0.") != std::string::npos && ipString_.back() != '0')
    {
        auto pos2 = buf.find('\0', pos);
        if (pos == std::string::npos)
        {
            status_ = Status::incomplete;
            return;                
        }

        domain_ = buf.substr(pos, pos2 - pos);
        evbuffer_drain(input, pos2 + 1);
            
        status_ = Status::success;            
        protocol_ = ProtocolInfo::Protocol::socks4a;
    }
    else
    {
        evbuffer_drain(input, pos + 1);
            
        status_ = Status::success;                        
        protocol_ = ProtocolInfo::Protocol::socks4;
    }
}

ProtocolInfo::Status ProtocolInfo::status() const
{
    return status_;        
}

ProtocolInfo::Protocol ProtocolInfo::protocol() const
{
    return protocol_;        
}
    
const std::string &ProtocolInfo::error() const
{
    return error_;        
}

void ProtocolInfo::responseSuccess(evbuffer *output)
{
    std::string resp(8, '\0');
    char reply_version = char(0);
    char reply_command = char(90);
    
    resp[0] = reply_version;
    resp[1] = reply_command;

    unsigned short port = htons(port_);
    unsigned int ip = htonl(ip_);
    
    memcpy(&resp[2], &port, 2);
    memcpy(&resp[4], &ip, 4);

    evbuffer_add(output, resp.data(), resp.size());
}

unsigned short ProtocolInfo::port() const
{
    return port_;        
}

unsigned int ProtocolInfo::ip() const
{
    return ip_;        
}

const std::string &ProtocolInfo::domain() const
{
    return domain_;        
}

std::ostream &operator<<(std::ostream &os, const ProtocolInfo &info)
{
    if (info.protocol_ == ProtocolInfo::Protocol::socks4)
    {
        os << "ProtocolInfo - version: " << info.version_
           << ", command: " << info.command_
           << ", destination port: " << info.port_
           << ", destination ip: " << info.ipString_;
    }
    else
    {
        os << "ProtocolInfo - version: " << info.version_
           << ", command: " << info.command_
           << ", destination port: " << info.port_
           << ", domain: " << info.domain_;
    }
    
    return os;    
}
