#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <memory>

#include <errno.h>
#include <arpa/inet.h>

#include <event2/buffer.h>

struct ProtocolInfo
{
    enum class Protocol {socks4, socks4a};
    
    char            version;
    char            command;
    unsigned short  port;
    unsigned int    ip;
    std::string     ipString;
    std::string     domain;    
    Protocol        protocol;    
};

std::unique_ptr<ProtocolInfo> retrieveProtocolInfo(evbuffer *input, std::string &error)
{    
    size_t len = evbuffer_get_length(input);    
    if (len < 9)
    {
        return nullptr;        
    }

    std::string buf(len, '\0');    
    evbuffer_copyout(input, (void *)buf.data(), len);

    auto pos = buf.find('\0');    
    if (pos == std::string::npos)
    {
        return nullptr;        
    }

    std::unique_ptr<ProtocolInfo> info{new ProtocolInfo};    
    
    info->version = buf[0];
    info->command = buf[1];
    info->port = ntohs(*reinterpret_cast<const unsigned short *>(&buf[2]));
    info->ip = ntohl(*reinterpret_cast<const unsigned int *>(&buf[4]));    

    info->ipString.resize(INET_ADDRSTRLEN);
    if (inet_ntop(AF_INET, &(info->ip), &(info->ipString[0]), INET_ADDRSTRLEN) == nullptr)
    {
        error = std::string("convert address error when calling inet_ntop(): ") + strerror(errno);
        return nullptr;        
    }

    info->protocol = ProtocolInfo::Protocol::socks4;            
    if (info->ipString.find("0.0.0.") != std::string::npos && info->ipString.back() != '0')
    {
        info->protocol = ProtocolInfo::Protocol::socks4a;
    }

    if (info->protocol == ProtocolInfo::Protocol::socks4a)
    {
        auto pos2 = buf.find('\0', pos);
        if (pos == std::string::npos)
        {
            return nullptr;            
        }
        info->domain = buf.substr(pos, pos2 - pos);
        evbuffer_drain(input, pos2);                
    }
    else
    {
        evbuffer_drain(input, pos);                        
    }
    
    return info;    
}
 
std::string protocolResponse(std::unique_ptr<ProtocolInfo> &info)
{
    std::string resp(8, '\0');
    char reply_version = char(4);
    char reply_command = char(90);
    
    resp[0] = reply_version;
    resp[1] = reply_command;

    unsigned short port = htons(info->port);
    unsigned int ip = htonl(info->ip);
    
    memcpy(&resp[2], &port, 2);
    memcpy(&resp[4], &ip, 4);
    
    return resp;    
}

#endif /* PROTOCOL_H */
