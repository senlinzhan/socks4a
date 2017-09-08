#ifndef SERVER_H
#define SERVER_H

#include <event2/buffer.h>
#include <event2/bufferevent.h>
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
    evconnlistener    *listener_;    // tcp listener
};

#endif /* SERVER_H */
