#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"
/*******************************************************参考《高性能服务器编程》p195*****************************************************/
class util_timer;
//用户数据结构
struct client_data
{
    //客户端socket地址
    sockaddr_in address;
    //socket描述符
    int sockfd;
    //定时器
    util_timer *timer;
};

//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    //超时时间
    time_t expire;
    //任务回调函数
    void (* cb_func)(client_data *);
    client_data *user_data;
    //前后指针
    util_timer *prev;
    util_timer *next;
};

//升序定时器链表
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();
    //将一个定时器添加到双向链表
    void add_timer(util_timer *timer);
    //当定时任务发生变化，调整定时器在链表的位置
    void adjust_timer(util_timer *timer);
    //从链表中删除一个定时器
    void del_timer(util_timer *timer);
    //滴答
    void tick();

private:
    //将目标定时器添加到节点lst_head之后的部分链表
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

//
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式：当事件发生，必须处理，同一事件不重复触发，选择开启EPOLLONESHOT：同一个连接由同一个线程处理
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    //
    static int *u_pipefd;//管道
    sort_timer_lst m_timer_lst;//定时器升序链表
    static int u_epollfd;//epoll描述符
    int m_TIMESLOT;//定时周期，每隔m_TIMESLOT就会触发定时信号
};

void cb_func(client_data *user_data);

#endif
