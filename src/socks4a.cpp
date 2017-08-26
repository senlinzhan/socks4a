#include "server.hpp"

int main(int argc, char *argv[])
{
    unsigned short port = 5273;
    
    Server server(port);
    server.run();
    
    return 0;
}
