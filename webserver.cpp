#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象数组
    users = new http_conn[MAX_FD];

    //root文件夹路径,存放各类资源
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器数组
    users_timer = new client_data[MAX_FD];
}

//析沟函数释放资源
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//服务器初始化
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;//端口号，默认9006
    m_user = user;//数据库登陆名
    m_passWord = passWord;//数据库密码
    m_databaseName = databaseName;//数据库名
    m_sql_num = sql_num;//数据库连接池数量
    m_thread_num = thread_num;//线程池线程数量
    m_log_write = log_write;//日志同步或异步，默认0，同步
    m_OPT_LINGER = opt_linger;//是否优雅关闭连接，默认0，不开启
    m_TRIGMode = trigmode;//listen connfd 触发方式，默认0，LT+LT
    m_close_log = close_log;//是否开启日志，默认开启
    m_actormodel = actor_model;//事件处理模式reactor 、模拟proactor,    默认是模拟proactor 
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)//异步写日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else//同步写日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    //初始化数据库信息：主机地址、用户名、密码、数据库名、端口号、连接数、日志开启与否等
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤：

    //ip4、TCP、默认阻塞
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接:若socket为阻塞，有数据待发送，延迟关闭，等待1s
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    //设置web的ip地址
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    //设置web端口号
    address.sin_port = htons(m_port);

    int flag = 1;
    
    //SO_REUSEEADDR强制使用被处于TIME_WAIT的连接socket地址
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    //将socket和address绑定
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    //监听
    ret = listen(m_listenfd, 20);
    assert(ret >= 0);

    //设置util工具类
    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //把m_listenfd添加到监听表中，且为m_LISTENTrigmode模式，默认LT
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    //创建一个双向管道，设置为非阻塞，添加到监听表中
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    //设置信号，其中SIGALRM、SIGTERM处理函数向管道m_pipefd[1]发送数据，统一事件源
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    //设置定时器，定时时间是TIMESLOT
    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //初始化这个连接下的http_conn
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    //创建一个定时器链表节点
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    //把该节点添加到升序链表中
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//处理一个新连接事件
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    //listen 为LT
    if (0 == m_LISTENTrigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        //加入定时器、初始化user[connfd]的http_conn并设为非阻塞
        timer(connfd, client_address);
    }

    else
    {
        // //listen 为ET
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}
//处理信号事件
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}
//处理可读事件
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列，由工作线程完成
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            
            if (1 == users[sockfd].improv)
            {
                //上一次未读到一点数据，则直接先处理定时任务：关闭连接
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //模拟proactor,由主线程完成读
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //把事件加入队列中，工作线程只需要i处理逻辑
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
//处理可写事件
void WebServer::dealwithwrite(int sockfd)
{
    //获取该socket的定时器
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }
        //users是http_conn *数组，将该socket指针是添加到请求队列，I/O、逻辑处理由工作线程完成
        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                //若上一次未写入一点数据，册直接处理定时任务：关闭连接
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //模拟proactor，I/O由主线程完成
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//运行
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            //连接被对方关闭、事件挂起、出现错误、
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}