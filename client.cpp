#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/ip.h>

static void die(const char* msg){
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    //AF_INET -> IP4, AF_INET6 -> IP6, SOCK_STREAM -> TCP, SOCK_DGRAM -> UDP
    if(fd<0){
        die("socket()");
    }

    // the client does not bind
    // because the OS automatically choose local IP and port

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);      // 127.0.0.1
    // INADDR_LOOPBACK is defined as 0x7f000001, which is the address 127.0.0.1.
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv){
        die("connect");
    }
    
    char msg[] = "hello";
    write(fd, msg, strlen(msg));

    char rbuf[64] ={};
    ssize_t n = read(fd, rbuf, sizeof(rbuf)-1);
    if(n<0){
        die("read");
    }
    printf("Server says: %s\n", rbuf);
    close(fd);
    return 0;
}