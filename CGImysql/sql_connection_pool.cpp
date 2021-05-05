#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	//主机地址、用户名、密码、数据库名、端口号、连接数、日志开启与否等
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;
	//创建MaxConn条数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			//写进错误日志
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//根据初始化的数据库信息进行登陆
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			//写进错误日志
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//加入到双向链表
		connList.push_back(con);
		++m_FreeConn;
	}
	//初始化信号量为最大连接数
	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	//P操作：若信号量大于0，减1；否则阻塞
	reserve.wait();
	//加锁，取出连接
	lock.lock();

	con = connList.front();
	connList.pop_front();

	--m_FreeConn;
	++m_CurConn;
	//释放锁
	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	//信号量V操作，原子加1
	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		//通过迭代器遍历，关闭数据库连接
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}
//析沟函数销毁数据库连接池
connection_pool::~connection_pool()
{
	DestroyPool();
}

//RALL实现连接获取与自动释放
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	//从连接池获取一个数据库连接
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}
//RALL机制销毁连接池
connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}