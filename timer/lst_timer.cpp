#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
//链表被销毁时，删除所有定时器
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}
//将目标定时器timer添加到链表中
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)
    {
        head = tail = timer;
        return;
    }
    //如果新的定时器超时时间小于头部节点，直接作为链表头部节点
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    //
    add_timer(timer, head);
}

//当某个定时任务发生变化，调整其在链表的位置
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    //超时值仍小于下一个定时任务
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    //若为链表头部节点，从链表取出，并将其重新插入链表中
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else//若不在头部，从链表取出，并将其重新插入链表中
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}
//从链表删除目标定时器
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    //只有一个定时器
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    //被删除的定时器在链表头部
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    //被删除的定时器在链表尾部
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    //被删除的定时器在链表中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

//定时任务处理函数
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    //获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;
    //遍历链表
    while (tmp)
    {
        //由于是升序若当前时间小于定时器的超时时间，则后面定时器都没定时器
        if (cur < tmp->expire)
        {
            break;
        }
        //到期，则调用回调函数，执行定时任务
        tmp->cb_func(tmp->user_data);
         //将处理后的定时器从链表容器删除，重置链表头节点
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

//被add_time、adjust_timer调用，将目标定时器插入到lst_head之后
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    //遍历当前节点之后的链表，按照超时时间找到插入位置
    while (tmp)
    {
        //将其插入到链表tmp节点之前
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    //遍历完，没找到插入位置，作为链表尾节点
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式：当事件发生，必须处理，同一事件不重复触发；选择开启EPOLLONESHOT：同一个连接由同一个线程处理
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数，管道加入到epoll事件表表，被监听
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    //将信号写入管道，通知主循环
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数：
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //将信号的处理函数都设置为handler
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    //调用滴答函数，遍历链表，检查所有定时任务
    m_timer_lst.tick();
    //因为一次alarm函数只会引起一次SIGALRM信号，需要重新定时，不断触发
    alarm(m_TIMESLOT);
}

//向客户端发送错误，并关闭该连接
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

//定时器回调函数，执行定时任务，删除非活动连接socket上注册事件，并关闭之
class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
