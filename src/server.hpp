#ifndef SERVER_H
#define SERVER_H

#include "protocol.hpp"
#include "tunnel.hpp"

#include <sys/socket.h>
#include <string.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

/*
  SOCKS 4 RFC: https://www.openssh.com/txt/socks4.protocol
  SOCKS 4A RFC: https://www.openssh.com/txt/socks4a.protocol
 */

std::unordered_map<bufferevent *, Tunnel> tunnels;

class Server
{
public:
    Server(unsigned short port)
        : port_(port),
          base_(event_base_new())
    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(port_);
        
        listener_ = evconnlistener_new_bind(
            base_,
            acceptCallback,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
            -1,
            reinterpret_cast<struct sockaddr *>(&sin),
            sizeof(sin)
        );

        if (listener_ == nullptr)
        {
            int err = EVUTIL_SOCKET_ERROR();
            std::cerr << "couldn't create listener: " 
                      << evutil_socket_error_to_string(err)
                      << std::endl;
            return;            
        }

        evconnlistener_set_error_cb(listener_, acceptErrorCallback);
    }

    void run()
    {
        event_base_dispatch(base_);        
    }
    
    ~Server()
    {
        event_base_free(base_);        
    }    
    
private:
    unsigned short     port_;        // server port
    event_base        *base_;        // event loop
    evconnlistener    *listener_;    // tcp listener

    static void acceptCallback(struct evconnlistener *listener, evutil_socket_t fd,
                               struct sockaddr *address, int socklen, void *arg)
    {
        evutil_make_socket_nonblocking(fd);
        
        auto *base = evconnlistener_get_base(listener);
        auto *b = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
        
        bufferevent_setcb(b, readCallback, nullptr, eventCallback, nullptr);
        bufferevent_enable(b, EV_READ|EV_WRITE);
    }

    static void acceptErrorCallback(struct evconnlistener *listener, void *arg)
    {
        auto *base = evconnlistener_get_base(listener);
        
        int err = EVUTIL_SOCKET_ERROR();
        std::cerr << "got an error on the listener: "
                  << evutil_socket_error_to_string(err)
                  << std::endl;
        
        evconnlistener_free(listener);

        // tells the event_base to stop looping and still running callbacks for any active events
        event_base_loopexit(base, NULL);   
    }
    
    static void readCallback(struct bufferevent *bev, void *arg)
    {
        auto *input = bufferevent_get_input(bev);
        auto *output = bufferevent_get_output(bev);
        
        auto iter = tunnels.find(bev);
        if (iter != tunnels.end())
        {
            iter->second.transferData(input);            
            return;        
        }

        ProtocolInfo info(input);        
        if (info.status() == ProtocolInfo::Status::success)
        {
            std::cout << "receive protocol info from client: " << info << std::endl;
            info.responseSuccess(output);
            
            auto *base = bufferevent_get_base(bev);
            tunnels.emplace(bev, Tunnel(base, bev, info));            
        }
        else if (info.status() == ProtocolInfo::Status::error)
        {
            std::cerr << "receive protocol info error: " << info.error() << std::endl;
            shutdown(bufferevent_getfd(bev), SHUT_WR);
        }
        else
        {
            // the content is incomplete, nothing to do
        }
    }

    static void eventCallback(struct bufferevent *bev, short events, void *arg)
    {
        if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
        {
            if (events & BEV_EVENT_ERROR)
            {
                int err = EVUTIL_SOCKET_ERROR();
                std::cerr << "receive error from client: "
                          << evutil_socket_error_to_string(err)
                          << std::endl;
            }
            else
            {
                std::cout << "connection close by client" << std::endl;

                auto iter = tunnels.find(bev);
                if (iter != tunnels.end())
                {
                    iter->second.shutdown();                
                    tunnels.erase(iter);
                }            
                bufferevent_free(bev);                
            }
        }        
    }
};

#endif /* SERVER_H */
