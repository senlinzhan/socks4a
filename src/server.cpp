#include "tunnel.hpp"

#include <assert.h>
#include <sys/socket.h>
#include <string.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>

#include "server.hpp"

std::unordered_map<bufferevent *, TunnelPtr> tunnels;

Server::Server(unsigned short port)
    : port_(port),
      base_(event_base_new()),
      dns_(evdns_base_new(base_, EVDNS_BASE_INITIALIZE_NAMESERVERS)),
      listener_(nullptr)
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

Server::~Server()    
{
    event_base_free(base_);        
}

void Server::run()    
{
    event_base_dispatch(base_);        
}

void Server::acceptCallback(struct evconnlistener *listener, evutil_socket_t fd,
                            struct sockaddr *address, int socklen, void *arg)
{
    evutil_make_socket_nonblocking(fd);
    
    auto *base = evconnlistener_get_base(listener);
    auto *b = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    
    bufferevent_setcb(b, readCallback, nullptr, eventCallback, nullptr);
    bufferevent_enable(b, EV_READ|EV_WRITE);
}

void Server::acceptErrorCallback(struct evconnlistener *listener, void *arg)
{
    auto *base = evconnlistener_get_base(listener);
        
    int err = EVUTIL_SOCKET_ERROR();
    std::cerr << "got an error on the listener: "
              << evutil_socket_error_to_string(err)
              << std::endl;
        
    evconnlistener_free(listener);

    // tells the event_base to stop looping and still running callbacks for any active events
    event_base_loopexit(base, nullptr);   
}
    
void Server::readCallback(struct bufferevent *bev, void *arg)
{
    auto *input = bufferevent_get_input(bev);
    auto *output = bufferevent_get_output(bev);
    
    auto iter = tunnels.find(bev);
    if (iter != tunnels.end())
    {
        auto tunnel = iter->second;
        
        assert(tunnel->status() != Tunnel::Status::ActiveClosed);
            
        tunnel->transferData(input);            
        return;        
    }

    ProtocolInfo info(input);        
    if (info.status() == ProtocolInfo::Status::success)
    {
        std::cout << "receive connection " << bev << " from client: " << info << std::endl;
        info.responseSuccess(output);
            
        auto *base = bufferevent_get_base(bev);
        assert(tunnels.find(bev) == tunnels.end());        
        tunnels[bev] = std::make_shared<Tunnel>(base, bev, info);            
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

void Server::eventCallback(struct bufferevent *serverConn, short events, void *arg)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        if (events & BEV_EVENT_ERROR)
        {
            int err = EVUTIL_SOCKET_ERROR();
            std::cerr << "connection " << serverConn << " receive error from client"
                      << evutil_socket_error_to_string(err)
                      << std::endl;
        }
        else
        {
            std::cout << "connection " << serverConn << " close by client" << std::endl;
            auto iter = tunnels.find(serverConn);
            assert(iter != tunnels.end());

            auto tunnel = iter->second;
            assert(tunnel->status() != Tunnel::Status::ActiveClosed);

            auto clientConn = tunnel->clientConn();                    
            if (tunnel->status() == Tunnel::Status::Connected)
            {
                std::cout << "shutdown tunnel connection " << clientConn << std::endl;
                int err = ::shutdown(bufferevent_getfd(clientConn), SHUT_WR);
                if (err == -1)
                {
                    std::cout << "shutdown error: " << strerror(errno) << std::endl;
                }
                tunnel->setStatus(Tunnel::Status::ActiveClosed);            
            }
            else
            {
                std::cout << "close connection " << serverConn << std::endl;
                tunnels.erase(iter);
                bufferevent_free(serverConn);
            }   
        }
    }        
}
