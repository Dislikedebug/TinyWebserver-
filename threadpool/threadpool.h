#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

//线程池构造函数，初始化：事件处理模型、线程总数、最大请求数、线程描述符数组、数据库连接池
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),
m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    //创建thread_number个线程，每个线程运行worker,分离线程，并把标识符存入m_threads数组中
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //脱离线程退出时，会自动释放占用的系统资源
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

//针对reactor,将请求添加到请求队列中
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    //记得加锁
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //V操作
    m_queuestat.post();
    return true;
}

//针对proactor,将请求添加到请求队列中
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//pool指向一个工作线程
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

//每个工作线程运行的函数
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        //P操作
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //从队列取出一个请求
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        //事件处理模式为Reactor ，I/O由工作线程完成
        if (1 == m_actor_model)
        {
            //读事件
            if (0 == request->m_state)
            {
                if (request->read_once())//
                {
                    request->improv = 1;
                    //创建一个连接RALL,指定一个数据库连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else//对方关闭连接
                {
                    
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else//写事件
            {
                if (request->write())//write()成功或socket缓冲区满了，等待下一次epoll触发
                {
                    request->improv = 1;
                }
                else//write(出错
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else//事件处理模式为模拟Proactor，直接让主线程负责I/O，工作线程只需要负责逻辑处理
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
