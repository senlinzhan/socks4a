#ifndef TUNNEL_H
#define TUNNEL_H

#include "protocol.hpp"

#include <arpa/inet.h>

#include <event2/dns.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

class Tunnel
{
public:
    Tunnel(event_base *base, bufferevent *server_bev, const ProtocolInfo &info)
        : base_(base),
          server_bev_(server_bev),
          info_(info),
          dns_base_(nullptr)
    {
        client_bev_ = bufferevent_socket_new(base_, -1, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(client_bev_, readCallback, nullptr, eventCallback, server_bev_);
        bufferevent_enable(client_bev_, EV_READ|EV_WRITE);

        if (info_.protocol() == ProtocolInfo::Protocol::socks4)
        {
            memset(&sin_, 0, sizeof(sin_));
            
            sin_.sin_family = AF_INET;
            sin_.sin_addr.s_addr = htonl(info_.ip());
            sin_.sin_port = htons(info_.port());

            bufferevent_socket_connect(client_bev_, reinterpret_cast<struct sockaddr *>(&sin_), sizeof(sin_));
        }
        else
        {
            dns_base_ = evdns_base_new(base_, 1);
            bufferevent_socket_connect_hostname(client_bev_, dns_base_, AF_INET, info_.domain().c_str(), info_.port());
        }
    }

    static void readCallback(struct bufferevent *bev, void *arg)
    {
        auto server_bev = static_cast<bufferevent *>(arg);        
        auto *input = bufferevent_get_input(bev);
        
        auto *output = bufferevent_get_output(server_bev);
        evbuffer_add_buffer(output, input);        
    }

    static void eventCallback(struct bufferevent *bev, short events, void *arg)
    {        
        if (events & BEV_EVENT_CONNECTED)
        {
            std::cout << "Connect ok" << std::endl;            
        }
        else if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
        {
            if (events & BEV_EVENT_ERROR)
            {
                int err = bufferevent_socket_get_dns_error(bev);
                if (err != 0)
                {
                    std::cerr << "DNS error: "
                              << evutil_gai_strerror(err)
                              << std::endl;  
                }
                else
                {
                    err = EVUTIL_SOCKET_ERROR();                    
                    std::cerr << "got an error from bufferevent: "
                              << evutil_socket_error_to_string(err)
                              << std::endl;                    
                }
            }
            //auto server_bev = static_cast<bufferevent *>(arg);
            //::shutdown(bufferevent_getfd(server_bev), SHUT_WR); 
        }
    }

    void shutdown()
    {
        ::shutdown(bufferevent_getfd(client_bev_), SHUT_WR);        
    }
    
    void transferData(struct evbuffer *input)
    {        
        auto *output = bufferevent_get_output(client_bev_);        
        evbuffer_add_buffer(output, input);        
    }
    
    ~Tunnel()
    {
        // bufferevent_free(client_bev_);        
    }
    
private:
    event_base           *base_;
    bufferevent          *server_bev_;    
    ProtocolInfo         info_;
    struct evdns_base    *dns_base_;
    
    bufferevent          *client_bev_;
    struct sockaddr_in   sin_;
};

#endif /* TUNNEL_H */
