#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../locker/locker.h"
#include "../log/log.h"

using namespace std;
class connection_pool{
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();

    static connection_pool *GetInstance();

    void init(string url,string user,string passWord,string DataBaseName,int port,int MaxConn,int close_log);
private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn; //最大连接数
    int m_CurConn; //当前已使用的连接数
    int m_FreeConn; //当前空闲的连接数
    locker lock;
    list<MYSQL *> connList;
    sem reserve;

public:
    string m_url;
    string m_Port;
    string m_User;
    string m_PassWord;
    string m_DatabaseName;
    int m_close_log;
};

class connectionRAII{
public:
    connectionRAII(MYSQL **con,connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif