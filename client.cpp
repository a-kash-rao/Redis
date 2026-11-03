#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<netinet/ip.h>
#include<string>
#include<vector>
#include "protocol.h"

// usage: ./client set k v
//        ./client get k
//        ./client zquery zset 0 "" 0 10

static void msg(const char* msg){
    fprintf(stderr, "%s\n", msg);
}

static void die(const char* msg){
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// read() can return fewer bytes than asked for (TCP is a byte stream),
// so keep reading until we have exactly n bytes
static int32_t read_full(int fd, uint8_t *buf, size_t n){
    while(n > 0){
        ssize_t rv = read(fd, buf, n);
        if(rv <= 0){
            return -1;  // error, or unexpected EOF
        }
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// same for write()
static int32_t write_all(int fd, const uint8_t *buf, size_t n){
    while(n > 0){
        ssize_t rv = write(fd, buf, n);
        if(rv <= 0){
            return -1;  // error
        }
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static void buf_append_u32(std::vector<uint8_t> &buf, uint32_t data){
    const uint8_t *p = (const uint8_t *)&data;
    buf.insert(buf.end(), p, p + 4);
}

// [len][nstr][len][str1][len][str2]...
static int32_t send_req(int fd, const std::vector<std::string> &cmd){
    size_t len = 4;
    for(const std::string &s : cmd){
        len += 4 + s.size();
    }
    if(len > k_max_msg){
        return -1;
    }

    std::vector<uint8_t> wbuf;
    buf_append_u32(wbuf, (uint32_t)len);
    buf_append_u32(wbuf, (uint32_t)cmd.size());
    for(const std::string &s : cmd){
        buf_append_u32(wbuf, (uint32_t)s.size());
        wbuf.insert(wbuf.end(), s.begin(), s.end());
    }
    return write_all(fd, wbuf.data(), wbuf.size());
}

// print one TLV value, returns the number of bytes consumed or -1
static int32_t print_response(const uint8_t *data, size_t size){
    if(size < 1){
        msg("bad response");
        return -1;
    }
    switch(data[0]){
    case TAG_NIL:
        printf("(nil)\n");
        return 1;
    case TAG_ERR:
        if(size < 1 + 8){
            msg("bad response");
            return -1;
        }
        {
            int32_t code = 0;
            uint32_t len = 0;
            memcpy(&code, &data[1], 4);
            memcpy(&len, &data[1 + 4], 4);
            if(size < 1 + 8 + (size_t)len){
                msg("bad response");
                return -1;
            }
            printf("(err) %d %.*s\n", code, (int)len, &data[1 + 8]);
            return 1 + 8 + len;
        }
    case TAG_STR:
        if(size < 1 + 4){
            msg("bad response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            if(size < 1 + 4 + (size_t)len){
                msg("bad response");
                return -1;
            }
            printf("(str) %.*s\n", (int)len, &data[1 + 4]);
            return 1 + 4 + len;
        }
    case TAG_INT:
        if(size < 1 + 8){
            msg("bad response");
            return -1;
        }
        {
            int64_t val = 0;
            memcpy(&val, &data[1], 8);
            printf("(int) %lld\n", (long long)val);
            return 1 + 8;
        }
    case TAG_DBL:
        if(size < 1 + 8){
            msg("bad response");
            return -1;
        }
        {
            double val = 0;
            memcpy(&val, &data[1], 8);
            printf("(dbl) %g\n", val);
            return 1 + 8;
        }
    case TAG_ARR:
        if(size < 1 + 4){
            msg("bad response");
            return -1;
        }
        {
            uint32_t len = 0;
            memcpy(&len, &data[1], 4);
            printf("(arr) len=%u\n", len);
            size_t arr_bytes = 1 + 4;
            for(uint32_t i = 0; i < len; ++i){
                int32_t rv = print_response(&data[arr_bytes], size - arr_bytes);
                if(rv < 0){
                    return rv;
                }
                arr_bytes += (size_t)rv;
            }
            printf("(arr) end\n");
            return (int32_t)arr_bytes;
        }
    default:
        msg("bad response");
        return -1;
    }
}

static int32_t read_res(int fd){
    // 4 bytes header
    std::vector<uint8_t> rbuf(4);
    errno = 0;
    if(read_full(fd, rbuf.data(), 4)){
        msg(errno == 0 ? "EOF" : "read() error");
        return -1;
    }
    uint32_t len = 0;
    memcpy(&len, rbuf.data(), 4);
    if(len > k_max_msg){
        msg("too long");
        return -1;
    }
    // reply body
    rbuf.resize(4 + len);
    if(read_full(fd, &rbuf[4], len)){
        msg("read() error");
        return -1;
    }
    int32_t rv = print_response(&rbuf[4], len);
    if(rv > 0 && (uint32_t)rv != len){
        msg("bad response");
        rv = -1;
    }
    return rv;
}

int main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr, "usage: %s <command> [args...]\n", argv[0]);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    //AF_INET -> IP4, AF_INET6 -> IP6, SOCK_STREAM -> TCP, SOCK_DGRAM -> UDP
    if(fd<0){
        die("socket()");
    }

    // the client does not bind
    // because the OS automatically choose local IP and port

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);      // 127.0.0.1
    // INADDR_LOOPBACK is defined as 0x7f000001, which is the address 127.0.0.1.
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if(rv){
        die("connect");
    }

    std::vector<std::string> cmd(argv + 1, argv + argc);
    int32_t err = send_req(fd, cmd);
    if(!err){
        err = read_res(fd);
    }
    close(fd);
    return err < 0 ? 1 : 0;
}
