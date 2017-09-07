#ifndef SERVER_H
#define SERVER_H

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/dns.h>
#include <event2/listener.h>

/*
  SOCKS 4 RFC: https://www.openssh.com/txt/socks4.protocol
  SOCKS 4A RFC: https://www.openssh.com/txt/socks4a.protocol
 */
class Server
{
public:
    Server(unsigned short port);
    ~Server();

    // disable the copy operations
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // run the event loop
    void run();

private:
    unsigned short     port_;        // server port
    event_base        *base_;        // event loop
    evdns_base        *dns_;         // dns object
    evconnlistener    *listener_;    // tcp listener

    static void acceptCallback(struct evconnlistener *listener, evutil_socket_t fd,
                               struct sockaddr *address, int socklen, void *arg);
    
    static void acceptErrorCallback(struct evconnlistener *listener, void *arg);
    
    static void readCallback(struct bufferevent *bev, void *arg);

    static void eventCallback(struct bufferevent *bev, short events, void *arg);
};

#endif /* SERVER_H */
