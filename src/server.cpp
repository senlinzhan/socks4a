#include "tunnel.hpp"

#include <assert.h>
#include <sys/socket.h>
#include <string.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>

#include "server.hpp"

// each server connection has a tunnel
std::unordered_map<bufferevent *, TunnelPtr> tunnels;

// called when the server accept new connection
static void acceptCallback(evconnlistener *listener, evutil_socket_t fd, sockaddr *address, int socklen, void *arg);

// called when server accept has an error
static void acceptErrorCallback(evconnlistener *listener, void *arg);

// called when enough data is read from the connection
static void readCallback(bufferevent *serverConn, void *arg);

// called when an event occurs on the connection
static void eventCallback(bufferevent *serverConn, short events, void *arg);

Server::Server(unsigned short port)
    : port_(port),
      base_(event_base_new()),
      listener_(nullptr)
{
    sockaddr_in sin;
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
        reinterpret_cast<sockaddr *>(&sin),
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

void acceptCallback(evconnlistener *listener, evutil_socket_t fd, sockaddr *address, int socklen, void *arg)
{
    evutil_make_socket_nonblocking(fd);
    
    auto base = evconnlistener_get_base(listener);
    auto serverConn = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    std::cout << "new server connection " << serverConn << std::endl;
    
    bufferevent_setcb(serverConn, readCallback, nullptr, eventCallback, nullptr);
    bufferevent_enable(serverConn, EV_READ|EV_WRITE);
}

void acceptErrorCallback(evconnlistener *listener, void *arg)
{
    auto base = evconnlistener_get_base(listener);
        
    int err = EVUTIL_SOCKET_ERROR();
    std::cerr << "got an error on the listener: "
              << evutil_socket_error_to_string(err)
              << std::endl;
        
    evconnlistener_free(listener);

    // tells the event_base to stop looping and still running callbacks for any active events
    event_base_loopexit(base, nullptr);   
}
    
void readCallback(bufferevent *serverConn, void *arg)
{
    auto base = bufferevent_get_base(serverConn);    
    auto input = bufferevent_get_input(serverConn);
    auto output = bufferevent_get_output(serverConn);
    
    auto iter = tunnels.find(serverConn);
    if (iter != tunnels.end())
    {
        // the server connection already has a tunnel
        auto &tunnel = iter->second;
        
        // make sure the tunnel can be written
        assert(tunnel->status() != Tunnel::Status::ActiveShutdown);

        // transfer the received data to the tunnel
        tunnel->transferData(input);
        
        return;        
    }

    // decode the protocol information from the received data
    ProtocolInfo info(input);
    
    if (info.status() == ProtocolInfo::Status::success)
    {        
        std::cout << "server connection " << serverConn
                  << " receive protocol info: " << info << std::endl;

        // tell the client we understand the protocol information
        info.responseSuccess(output);

        // create a tunnel to connect to a remote server
        tunnels[serverConn] = std::unique_ptr<Tunnel>(new Tunnel(base, serverConn, info));
    }
    else if (info.status() == ProtocolInfo::Status::error)
    {
        std::cerr << "server connection " << serverConn
                  << " receive invalid protocol info: " << info.error() << std::endl;

        bufferevent_free(serverConn);        
    }
    else
    {
        // the content is incomplete, nothing to do
    }
}

void eventCallback(struct bufferevent *serverConn, short events, void *arg)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        if (events & BEV_EVENT_ERROR)
        {
            int err = EVUTIL_SOCKET_ERROR();
            std::cerr << "server connection " << serverConn << " receive error: "
                      << evutil_socket_error_to_string(err)
                      << std::endl;
        }
        else
        {
            assert(events & BEV_EVENT_EOF);
            
            auto iter = tunnels.find(serverConn);
            assert(iter != tunnels.end());

            auto &tunnel = iter->second;
            assert(tunnel->status() != Tunnel::Status::ActiveShutdown);
            
            if (tunnel->status() == Tunnel::Status::Connected || tunnel->status() == Tunnel::Status::Pending)
            {
                std::cout << "server connection " << serverConn << " shutdown by the remote client, "
                          << "so we shutdown it's client connection " << tunnel->clientConn() << std::endl;

                // in order to safely shut down the tcp connection,
                // we need to ensure that all data is sent 
                // before shutdown() the tcp connection
                tunnel->shutdownOnWriteComplete();
                
                tunnel->setStatus(Tunnel::Status::ActiveShutdown);
            }
            else
            {
                assert(tunnel->status() == Tunnel::Status::PassiveShutdown);
                std::cout << "server connection " << serverConn << " closed by the remote client, "
                          << "so we close itself and it's client connection " << tunnel->clientConn() << std::endl;

                tunnels.erase(iter);
                bufferevent_free(serverConn);                
            }
        }
    }        
}
