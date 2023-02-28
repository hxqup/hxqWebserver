// 同步日志：日志写入函数与工作线程串行执行，由于涉及到I/O操作
// 当单条日志比较大时，同步模式会阻塞整个处理流程，服务器所能处理的并发能力会下降

// 异步日志：将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
    // 日志行数
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    if(m_fp != NULL){
        fclose(m_fp);
    }
}

// 可选参数有日志文件、文件缓冲区大小、最大行数以及阻塞队列长度
bool Log::init(const char *file_name,int close_log,int log_buf_size,int split_lines,int max_queue_size){
    // 如果设置了max_queue_size则设置了异步
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        pthread_create(&tid,NULL,flush_log_thread,NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf,'\0',m_log_buf_size);
    m_split_lines = split_lines;
    
    // time_t是UNIX时间戳，定义为从格林威治时间1970年01月01日00时00分00秒起至现在的总秒数
    time_t t = time(NULL);
    
    // tm直接存储年月日的一个结构，年份是从1900年起至今多少年，月份从0开始，星期也是从0开始
    // localtime进行转化
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 在file_name中搜索最后一次出现'/'的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if(p == NULL){
        // 将...按照format形式写入log_full_name,并提供了一个size防止缓冲区溢出
        // %02d按照2位的宽度输出，不够两位时左边补0
        snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday,file_name);
    }
    else{
        strcpy(log_name,p + 1);
        strncpy(dir_name,file_name,p - file_name + 1);
        snprintf(log_full_name,255,"%s%d_%02d_%02d_%s",dir_name,my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday,log_name);
    }

    m_today = my_tm.tm_mday;

    // 使用给定的模式打开文件，a表示追加模式
    m_fp = fopen(log_full_name,"a");
    if(m_fp == NULL){
        return false;
    }
    return true;
}


void Log::write_log(int level,const char* format,...){
    // timeval中一个元素为秒，一个为微秒
    struct timeval now = {0,0};
    // 获取当前的时间，可以精确到微秒
    gettimeofday(&now,NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch(level){
        case 0:
        strcpy(s,"[debug]:");
        break;
        case 1:
        strcpy(s,"[info]:");
        break;
        case 2:
        strcpy(s,"[warn]:");
        break;
        case 3:
        strcpy(s,"[erro]:");
        break;
        default:
        strcpy(s,"[info]:");
        break;
    }

    m_mutex.lock();
    m_count++;

    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        // 格式化日志中的时间部分
        snprintf(tail,16,"%d_%02d_%02d_",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday);

        if(m_today != my_tm.tm_mday){
            snprintf(new_log,255,"%s%s%s",dir_name,tail,log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else{
            // %lld用于longlong
            // 超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log,255,"%s%s%s.%lld",dir_name,tail,log_name,m_count / m_split_lines);
        }
        m_fp = fopen(new_log,"a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst,format);

    string log_str;
    m_mutex.lock();

    int n = snprintf(m_buf,48,"%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                    my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday,
                    my_tm.tm_hour,my_tm.tm_min,my_tm.tm_sec,now.tv_usec,s);
    
    // vsnprintf的可变参数换成了snprintf
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf + n,m_log_buf_size - n - 1,format,valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    if(m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    else{
        m_mutex.lock();
        fputs(log_str.c_str(),m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}