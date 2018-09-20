
// socketutil.h

#ifndef _socketutil_H_
#define _socketutil_H_

#ifdef __cplusplus
extern 'C'
{
#endif

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

// 设置I/O为非阻塞模式
void activate_nonblock(int fd);

// 设置I/O为阻塞模式
void deactivate_nonblock(int fd);

// 读超时检测函数，不含读操作
int read_timeout(int fd, unsigned int wait_seconds);

// 写超时检测函数, 不包含写操作
int write_timeout(int fd, unsigned int wait_seconds);

// 带超时的accept
int accept_timeout(int fd, struct sockaddr_in *addr, unsigned int wait_seconds);

// 带连接超时的connect函数
int connect_timeout(int fd, struct sockaddr_in *addr, unsigned int wait_seconds);

// 每次从缓冲区中读取n个字符
ssize_t readn(int fd, void *buf, size_t count);

// 每次往缓冲区写入n个字符
ssize_t writen(int fd, const void *buf, size_t count);

// 检查内存缓冲区中的字节数, 但不读取数据(缓冲区中的数据不会较少)
ssize_t recv_peek(int sockfd, void *buf, size_t len);

// 每次读一行
ssize_t readline(int sockfd, void *buf, size_t maxline);


#ifdef __cpluspluse
}
#endif


#endif /* _SYS_UTIL_H_ */
