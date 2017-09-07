set(SRCS
    socks4a.cpp
    server.cpp
    tunnel.cpp
    protocol.cpp
)

add_executable(socks4a ${SRCS})
target_link_libraries(socks4a event)
