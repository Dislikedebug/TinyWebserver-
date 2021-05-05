#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    //读缓冲区
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区
    static const int WRITE_BUFFER_SIZE = 1024;
    //HTTP请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机：正在分析请求行、分析头部字段、分析内容
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //服务器http处理请求结果：、
    enum HTTP_CODE
    {
        NO_REQUEST,//请求不完整需要继续读取
        GET_REQUEST,//获得一个完整的客户请求
        BAD_REQUEST,//客户请求有错误错误
        NO_RESOURCE,//没有访问的资源
        FORBIDDEN_REQUEST,//客户没有访问权限
        FILE_REQUEST,//文件请求
        INTERNAL_ERROR,//服务器内部错误
        CLOSED_CONNECTION//客户端已经关闭连接
    };
    //从状态机：行读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化新接受的连接
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭连接
    void close_conn(bool real_close = true);
    //处理客户请求
    void process();
    //非阻塞读操作
    bool read_once();
    //非阻塞写操作
    bool write();

    sockaddr_in *get_address()
    {
        return &m_address;
    }
    
    //初始化一个数据库连接
    void initmysql_result(connection_pool *connPool);

    
    int timer_flag;
    int improv;


private:
    //初始化连接
    void init();
    //从状态：用以解析HTTP请求
    HTTP_CODE process_read();
    //填充HTTP应答
    bool process_write(HTTP_CODE ret);

    /*以下一组函数被process_read调用*/
    //分析请求行
    HTTP_CODE parse_request_line(char *text);
    //分析头部字段
    HTTP_CODE parse_headers(char *text);
    //分析HTTP请求的入口函数
    HTTP_CODE parse_content(char *text);
    //
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();

    /*下面一组函数被process_write调用*/

    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    //所有socket上事件被注册到一个epoll内核事件表中
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;
    //该http关联的mysql连接
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    //该HTTP连接的socket标识符
    int m_sockfd;
    //该HTTP连接对方的socket地址
    sockaddr_in m_address;
    //读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    //标识读缓冲区已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    //当前正在分析的字符在读缓冲区的位置
    int m_checked_idx;
    //但前正在解析的行的起始位置
    int m_start_line;
    //写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    //写缓冲区待发送的字节数
    int m_write_idx;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //客户请求的目标文件的完整路径，其内容等于doc_root+m_url,doc_root是网站根目录
    char m_real_file[FILENAME_LEN];
    //客户请求的目标文件的文件名
    char *m_url;
    //HTTP版本
    char *m_version;
    //主机名
    char *m_host;
    //http请求消息体的长度
    int m_content_length;
    //HTTP请求是否保持长连接
    bool m_linger;

    //客户请求的目标文件被mmap到内存的起始位置
    char *m_file_address;
    //目标文件的状态：是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct stat m_file_stat;
    //集中写：将多个分散的内存数据一起写入文件描述符中
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;//剩余发送的字节数
    int bytes_have_send;//已发送字节数
    char *doc_root;

    map<string, string> m_users;//用户名：密码
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];//数据库用户名
    char sql_passwd[100];//数据库用户密码
    char sql_name[100];//表名
};

#endif
