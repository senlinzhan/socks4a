#include "tunnel.hpp"

#include <string.h>
#include <assert.h>

#include <functional>
#include <unordered_map>

extern std::unordered_map<bufferevent *, TunnelPtr> tunnels;

static void readCallback(bufferevent *clientConn, void *arg);
static void eventCallback(bufferevent *clientConn, short events, void *arg);

static void buffereventShutdown(bufferevent *conn, void *arg)
{
    ::shutdown(bufferevent_getfd(conn), SHUT_WR);
}

static void buffereventShutdownOnWriteComplete(bufferevent *conn)
{
    bufferevent_data_cb readCb;
    bufferevent_event_cb eventCb;
    void *arg;    
    
    bufferevent_getcb(conn, &readCb, nullptr, &eventCb, &arg);
    bufferevent_setcb(conn, readCb, buffereventShutdown, eventCb, arg);
}

Tunnel::Tunnel(event_base *base, bufferevent *serverConn, const ProtocolInfo &info)
    : base_(base),
      dns_(evdns_base_new(base_, EVDNS_BASE_INITIALIZE_NAMESERVERS)),
      serverConn_(serverConn),
      info_(info),
      clientConn_(nullptr),
      status_(Status::Pending)      
{
    clientConn_ = bufferevent_socket_new(base_, -1, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(clientConn_, readCallback, nullptr, eventCallback, this);
    bufferevent_enable(clientConn_, EV_READ|EV_WRITE);

    std::cout << "create client connection " << clientConn_ 
              << " for server connection " << serverConn << std::endl;
    
    if (info_.protocol() == ProtocolInfo::Protocol::socks4)
    {
        memset(&sin_, 0, sizeof(sin_));
            
        sin_.sin_family = AF_INET;
        sin_.sin_addr.s_addr = htonl(info_.ip());
        sin_.sin_port = htons(info_.port());

        bufferevent_socket_connect(clientConn_, reinterpret_cast<struct sockaddr *>(&sin_), sizeof(sin_));
    }
    else
    {
        bufferevent_socket_connect_hostname(clientConn_, dns_, AF_INET, info_.domain().c_str(), info_.port());
    }
}

Tunnel::~Tunnel()
{
    bufferevent_free(clientConn_);    
}

void Tunnel::shutdownOnWriteComplete()
{
    buffereventShutdownOnWriteComplete(clientConn_);
}
    
void Tunnel::transferData(evbuffer *input)
{
    auto output = bufferevent_get_output(clientConn_);        
    evbuffer_add_buffer(output, input);        
}

bufferevent *Tunnel::serverConn()
{
    return serverConn_;    
}

bufferevent *Tunnel::clientConn()
{
    return clientConn_;    
}

Tunnel::Status Tunnel::status() const
{
    return status_;    
}

void Tunnel::setStatus(Status status)
{
    status_ = status;    
}

void readCallback(bufferevent *clientConn, void *arg)
{
    auto tunnel = static_cast<Tunnel *>(arg);
    auto serverConn = tunnel->serverConn();

    assert(tunnel->status() != Tunnel::Status::PassiveShutdown);
    
    auto input = bufferevent_get_input(clientConn);
    auto output = bufferevent_get_output(serverConn);
    evbuffer_add_buffer(output, input);        
}

void eventCallback(bufferevent *clientConn, short events, void *arg)
{
    auto tunnel = static_cast<Tunnel *>(arg);
    
    if (events & BEV_EVENT_CONNECTED)
    {
        std::cout << "client connection " << clientConn
                  << " connected" << std::endl;
        
        tunnel->setStatus(Tunnel::Status::Connected);        
    }
    else if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
    {
        if (events & BEV_EVENT_ERROR)
        {
            int err = bufferevent_socket_get_dns_error(clientConn);
            if (err != 0)
            {
                std::cerr << "DNS error: "
                          << evutil_gai_strerror(err)
                          << std::endl;  
            }
            else
            {
                err = EVUTIL_SOCKET_ERROR();                    
                std::cerr << "client connection " << clientConn << " received error: "
                          << evutil_socket_error_to_string(err) << std::endl;
            }
        }
        else
        {
            assert(events & BEV_EVENT_EOF);
            assert(tunnel->status() == Tunnel::Status::Connected ||
               tunnel->status() == Tunnel::Status::ActiveShutdown);

            auto serverConn = tunnel->serverConn();
            assert(tunnels.find(serverConn) != tunnels.end());
            
            if (tunnel->status() == Tunnel::Status::Connected)
            {
                std::cout << "client connection " << clientConn << " shutdown by the remote server, "
                          << "so we shutdown it's server connection " << serverConn << std::endl;

                buffereventShutdownOnWriteComplete(tunnel->serverConn());                
                tunnel->setStatus(Tunnel::Status::PassiveShutdown);
            }
            else
            {
                std::cout << "client connection " << clientConn << " closed by the remote server, "
                          << "so we close itself and it's server connection " << serverConn << std::endl;
                
                auto iter = tunnels.find(serverConn);
                assert(iter != tunnels.end());
                tunnels.erase(iter);
                
                bufferevent_free(serverConn);
            }            
        }
    }
}
