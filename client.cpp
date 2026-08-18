#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/ip.h>

static void die(char* msg){
    int err = errno;
    fprintf(stderr, "%[d] %s\n", err, msg);
    abort();
}

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    //AF_INET -> IP4, AF_INET6 -> IP6, SOCK_STREAM -> TCP, SOCK_DGRAM -> UDP
    
}