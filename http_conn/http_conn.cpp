#include "http_conn.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this requested file.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";



locker m_lock;
map<string,string> users;

void http_conn::initmysql_result(connection_pool* connPool){
    MYSQL *mysql = NULL;
    connectionRAII mysqlconn(&mysql,connPool);

    // 在user表中检索username,passwd数据，浏览器端输入
    // 执行由“Null终结的字符串”查询指向的SQL查询
    if(mysql_query(mysql,"SELECT username,passwd FROM user"))
    {
        LOG_ERROR("select error:%s\n",mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    // 获取表中的列名字，它返回的是mysql field类型的数组，用一次就能获取所有列名
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    // 获取一行的数据，但是获取一行后自动后移
    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

int setnonblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    // EPOLLRDHUP用于判断对端TCP连接或者写操作是否关闭
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close){
    if(real_close && (m_sockfd != -1))
    {
        LOG_INFO("close %d\n",m_sockfd);
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd,const sockaddr_in& addr,char* root,
                    int close_log,string user,string passwd,string sqlname){
    m_sockfd = sockfd;
    m_address = addr;

    // 要是加上reuseaddr 得到webbench请求量为0

    // int reuse = 1;
    // SO_REUSEADDR强制使用被处于TIME_WAIT状态的连接占用的socket地址,即tcp_tw_reuse，实际使用时应关闭，这里作调试用
    // ！！！但是如果不开启这个的话，当建立连接，再次epoll_wait的时候会阻塞
    // setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,sockfd,true);
    m_user_count++;

    doc_root = root;
    m_close_log = close_log;
    
    strcpy(sql_user,user.c_str());
    strcpy(sql_passwd,passwd.c_str());
    strcpy(sql_name,sqlname.c_str());

    init();
}

void http_conn::init(){
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}

// 从状态机
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_idx < m_read_idx;++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            if((m_checked_idx + 1) == m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r'){
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_sockfd,m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            return false;
        }
        else if(bytes_read == 0){
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// 解析HTTP请求行，获得请求方法、目标URL以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    m_url = strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }
    LOG_INFO("get m_url");
    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method,"GET") == 0){
        m_method = GET;
    }
    else if(strcasecmp(method,"POST") == 0){
        m_method = POST;
        cgi = 1;
    }
    else{
        return BAD_REQUEST;
    }

    m_url += strspn(m_url," \t");
    // 找到m_url和"\t"第一个匹配的字符
    m_version = strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    // 找到m_version和"\t"第一个不匹配的字符
    m_version += strspn(m_version," \t");
    if(strcasecmp(m_version,"HTTP/1.1") != 0){
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url,"http://",7) == 0){
        m_url += 7;
        // 找到m_url第一次出现'/'的位置
        m_url = strchr(m_url,'/');
    }

    if(strncasecmp(m_url,"https://",8) == 0){
        m_url += 8;
        m_url = strchr(m_url,'/');
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }
    // 当url为/时，显示判断界面
    if(strlen(m_url) == 1) strcat(m_url,"judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    // 遇到空行说明头部字段解析完毕
    if(text[0] == '\0'){
        // 如果HTTP请求有消息体，还需要提取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if(strncasecmp(text,"connection:",11) == 0){
        text += 11;
        text += strspn(text," \t");
        if(strcasecmp(text,"keep-alive") == 0){
            m_linger = true;
        }
    }
    // 处理Content-Length头部字段
    else if(strncasecmp(text,"Content-Length:",15) == 0){
        text += 15;
        text += strspn(text," \t");
        m_content_length = atol(text);
    }
    // 处理host字段
    else if(strncasecmp(text,"Host",5) == 0){
        text += 5;
        text += strspn(text," \t");
        m_host = text;
    }
    else{
        LOG_INFO("oop!unknown header %s",text);
    }
    return NO_REQUEST;
}

// 没有真正解析HTTP请求的消息体，只是判断它是否被完整读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK){
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("got 1 http line: %s",text);
    
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求，我们就分析目标文件的属性。如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址处，并
// 告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    // 在电脑中的实际位置
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);

    const char *p = strrchr(m_url,'/');

    // 处理post
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')){
        char flag = m_url[1];

        char* m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/");
        strcat(m_url_real,m_url + 2);
        strncpy(m_real_file + len,m_url_real,FILENAME_LEN - len - 1);
        free(m_url_real);

        // 提取用户名和密码
        char name[100],password[100];
        int i;

        // 以&为分隔符，前面的为用户名 user=123&password=123,网络知识
        for(i = 5;m_string[i] != '&';++i){
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10;m_string[i] != '\0';++i,++j){
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if(*(p + 1) == '3'){
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert,"'");
            strcat(sql_insert,name);
            strcat(sql_insert,"','");
            strcat(sql_insert,password);
            strcat(sql_insert,"')");

            if(users.find(name) == users.end()){
                m_lock.lock();
                int res = mysql_query(mysql,sql_insert);
                users.insert(pair<string,string>(name,password));
                m_lock.unlock();

                if(!res){
                    strcpy(m_url,"/log.html");
                }
                else{
                    strcpy(m_url,"registerError.html");
                }
            }
            else{
                strcpy(m_url,"/registerError.html");
            }
        }
        // 如果是登录
        else if(*(p + 1) == '2')
        {
            if(users.find(name) != users.end() && users[name] == password){
                strcpy(m_url,"/welcome.html");
            }
            else{
                strcpy(m_url,"/logError.html");
            }
        }
    }

    if(*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/register.html");
        strncpy(m_real_file + len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/log.html");
        strncpy(m_real_file + len,m_url_real,strlen(m_url_real));

        free(m_url_real);
    }
    else if(*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/picture.html");
        strncpy(m_real_file + len,m_url_real,strlen(m_url_real));

        free(m_url_real);
    }
    else if(*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/video.html");
        strncpy(m_real_file + len,m_url_real,strlen(m_url_real));

        free(m_url_real);
    }
    else if(*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real,"/fans.html");
        strncpy(m_real_file + len,m_url_real,strlen(m_url_real));
    }
    else
        strncpy(m_real_file + len,m_url,FILENAME_LEN - len - 1);

    if(stat(m_real_file,&m_file_stat) < 0){
        return NO_RESOURCE;
    }

    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    int fd = open(m_real_file,O_RDONLY);
    // PROT_READ表示内存段可读，MAP_PRIVATE表示内存段为调用进程所私有，对该内存段的修改不会反映到被映射的文件中。
    // m_file_address为映射的地址，第一个形参为0代表让系统选内存位置，m_file_stat代表将文件中多大映射到内存中，fd代表要映射到内存中的文件，最后一个参数为文件映射的偏移
    m_file_address = (char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write(){
    int temp = 0;

    // 要发送的数据长度为0，表示响应报文为空，一般不会出现这种情况
    if(bytes_to_send == 0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }

    // 针对大文件传输作出调整
    while(1){
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        // writev以顺序iov[0]、iov[1]至iov[iovcnt-1]从各缓冲区中聚集输出数据到fd
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if(temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。虽然在此期间，服务器没法立即接收到同一客户的下一个请求，但这可以保证连接的完整性
            if(errno = EAGAIN){
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;

        // m_iv[0]为写缓冲区,m_iv[1]为文件地址
        // 第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[1].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0){
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else{
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return false;
            }
        }
    }
}


// 往写缓冲区中写入待发送的数据
bool http_conn::add_response(const char* format,...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list,format);
    // 第一个参数是写入的位置，第二个参数是大小
    int len = vsnprintf(m_write_buf + m_write_idx,WRITE_BUFFER_SIZE - 1 - m_write_idx,format,arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s",m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s \r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers(int content_len){
    return add_content_length(content_len) && 
    add_linger() &&
    add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-length:%d\r\n",content_len);
}

bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n","text/html");
}

bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char* content){
    return add_response("%s",content);
}

bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else{
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}