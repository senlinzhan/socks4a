#ifndef TUNNEL_H
#define TUNNEL_H

#include "protocol.hpp"

#include <arpa/inet.h>

#include <memory>

#include <event2/dns.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

class Tunnel;
using TunnelPtr = std::unique_ptr<Tunnel>;

class Tunnel
{
public:
    Tunnel(event_base *base, bufferevent *serverConn, const ProtocolInfo &info);
    ~Tunnel();
    
    // disable the copy operations
    Tunnel(const Tunnel &) = delete;
    Tunnel &operator=(const Tunnel &) = delete;
    
    void shutdown();
    
    void transferData(evbuffer *input);

    bufferevent *serverConn();
    bufferevent *clientConn();    
    
    enum class Status { Pending, Connected, ActiveShutdown, PassiveShutdown };

    Status status() const;
    void setStatus(Status status);
    
private:    
    event_base           *base_;
    evdns_base           *dns_;    
    bufferevent          *serverConn_;
    ProtocolInfo         info_;
    bufferevent          *clientConn_;
    Status               status_;            
    sockaddr_in          sin_;
};

#endif /* TUNNEL_H */
