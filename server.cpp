#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/ip.h>

static void msg(const char* msg){
    fprintf(stderr, "%s\n", msg);
}

static void die(const char* msg){
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void do_something(int connfd){
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf)-1);
    if(n<0){
        msg("read() error");
        return;
    }
    fprintf(stderr, "client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    // AF_INET -> IP4, AF_INET6 -> IP6, SOCK_STREAM -> TCP, SOCK_DGRAM -> UDP
    if(fd<0){ die("socket()"); }
    // socket is a handle to refer to a connection



    int val=1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    // REUSEADDR : when a TCP network is closed by a server, it remains open for some time
    // as there might be some packet delayed(so it waits for that), so if the server tries to bind
    // again to that addr, it won't be able to do so, se REUSEADDR(val=1) tells the OS to allow it
    // to conect to that same network again, ALWAYS DO IT, basically, without it, we can't restart the
    // server immediately again




    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;          //IPV4 address
    addr.sin_port = ntohs(1234);        //port(in big-endian)=1234
    // most cpus use little-endian, network protocols use big-endian
    // htons -> host to network short -> little-endian to big
    // ntohs -> big-endian to little
    // big-endian -> read left to right(normal), little -> opposite
    // eg. 0x0304, little endian: 04 03, big: 03 04
    addr.sin_addr.s_addr = ntohl(0);    // IP = 0.0.0.0
    // sin_addr = IPv4
    // s_addr = IPv4 in big-endian
    // it means to listen on all IP addresses 
    // 0.0.0.0:1234 basically means any_ip_address:1234
    // htonl -> host to network long, similiar to htons and vice-versa
    // short -> unit16, long -> unit32

    // bind connects our socket to the ip address and port
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv) { die("bind()"); }




    // listen
    // listen turns the socket into a listening socket
    // previous steps were just passing the parametere
    // now the socket is being created
    rv = listen(fd, SOMAXCONN);
    if(rv){ die("listen()"); }
    // SOMAXCONN, size of queue(4096 on linux)





    while(true){
        //accept
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if(connfd<0){ continue;}      // error

        do_something(connfd);
        close(connfd);
    }

    return 0;
    
}