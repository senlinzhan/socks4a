#include "server.hpp"

#include <stdlib.h>
#include <signal.h>

typedef void (*sighandler_t)(int);

sighandler_t Signal(int signum, sighandler_t handler)
{
    struct sigaction action, old_action;
 
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);  // Block sigs of type being handled
    action.sa_flags = SA_RESTART;  // Restart syscalls if possible

    if (sigaction(signum, &action, &old_action) < 0) 
    {
        perror("sigaction");
        exit(-1);
    }
    
    return (old_action.sa_handler);
}

int main(int argc, char *argv[])
{
    Signal(SIGPIPE, SIG_IGN);

    unsigned short port = 5274;
    
    Server server(port);
    server.run();
    
    return 0;
}
