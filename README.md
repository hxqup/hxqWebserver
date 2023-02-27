# 基于Linux的轻量级web服务器

## 主要功能

* 利用socket来实现不同主机之间的通信；
* 利用epoll技术实现I/O多路复用，提高效率；
* 对浏览器的get请求进行处理，使用有限状态机逻辑高效解析HTTP报文；
* 利用多线程机制设计了一个线程池，增加并行服务数量；
* 利用定时器将不活跃的客户访问及时关闭，利用数据库连接池减少连接

