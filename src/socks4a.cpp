#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdint.h>

#include <iostream>
#include <string>

/*
  SOCKS 4 RFC: https://www.openssh.com/txt/socks4.protocol
  SOCKS 4A RFC: https://www.openssh.com/txt/socks4a.protocol
 */
void echo_read_cb(struct bufferevent *bev, void *ctx)
{
    auto *input = bufferevent_get_input(bev);    
    size_t len = evbuffer_get_length(input);
    
    if (len < 9)
    {
        return;        
    }

    std::string buf(len, '\0');    
    evbuffer_copyout(input, (void *)buf.data(), len);    

    auto pos = buf.find('\0');    
    if (pos == std::string::npos)
    {
        return;        
    }
    
    int version = buf[0];
    int command = buf[1];    

    unsigned short dest_port = ntohs(*reinterpret_cast<unsigned short *>(&buf[2]));
    unsigned int dest_ip = ntohl(*reinterpret_cast<unsigned int *>(&buf[4]));    

    std::string ip_str(INET_ADDRSTRLEN, '\0');
    if (inet_ntop(AF_INET, (void *)&dest_ip, &ip_str[0], INET_ADDRSTRLEN) == nullptr)
    {
        std::cerr << "Convert address error when call inet_ntop(): " << strerror(errno) << std::endl;
        bufferevent_free(bev);
        return;        
    }
    
    std::cout << "Receive connection, version: " << version 
              << ", command: " << command 
              << ", destination port: " << dest_port
              << ", destination ip: " << ip_str
              << std::endl;

    bool is_socks4a = false;
    if (ip_str.find("0.0.0.") != std::string::npos && ip_str.back() != '0')
    {
        is_socks4a = true;        
    }

    std::string domain;    
    if (is_socks4a)
    {
        auto pos2 = buf.find('\0', pos);
        if (pos == std::string::npos)
        {
            return;            
        }
        domain = buf.substr(pos, pos2 - pos);
        std::cout << "Domain: " << domain;        
    }
}

void echo_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
    {
        int err = EVUTIL_SOCKET_ERROR();
        std::cerr << "Got an error from bufferevent: "
                  << evutil_socket_error_to_string(err)
                  << std::endl;
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        bufferevent_free(bev);
    }
}

void accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
                    struct sockaddr *address, int socklen, void *arg)
{
    evutil_make_socket_nonblocking(fd);
    
    auto *base = evconnlistener_get_base(listener);
    auto *b = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    
    bufferevent_setcb(b, echo_read_cb, nullptr, echo_event_cb, nullptr);
    bufferevent_enable(b, EV_READ|EV_WRITE);
}

sockaddr_in create_sockaddr(short port)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);

    return sin;    
}

void accept_error_cb(struct evconnlistener *listener, void *arg)
{
    auto *base = evconnlistener_get_base(listener);

    int err = EVUTIL_SOCKET_ERROR();
    std::cerr << "Got an error on the listener: "
              << evutil_socket_error_to_string(err)
              << std::endl;

    evconnlistener_free(listener);
    event_base_loopexit(base, NULL);
}

int main()
{
    auto sin = create_sockaddr(5273);    
    auto *base = event_base_new();
    auto *listener = evconnlistener_new_bind(
        base,
        accept_conn_cb,
        nullptr,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
        -1,
        reinterpret_cast<struct sockaddr *>(&sin),
        sizeof(sin));
    
    if (listener == nullptr)
    {
        int err = EVUTIL_SOCKET_ERROR();
        std::cerr << "Couldn't create listener: " 
                  << evutil_socket_error_to_string(err)
                  << std::endl;
        return 1;
    }
    
    evconnlistener_set_error_cb(listener, accept_error_cb);
    event_base_dispatch(base);
    
    return 0;
}
