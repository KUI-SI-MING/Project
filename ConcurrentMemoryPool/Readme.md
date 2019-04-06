# <font color=black size=6 face="微软雅黑">多核多线程环境下的并发内存池</font>
- <font color=black size=4 face="微软雅黑">Thread Cache线程独有的缓存，线程申请无需加锁，解决并发问题
- <font color=black size=4 face="微软雅黑">Central Cache所有线程共享，临界资源需要加锁处理，可以均衡资源
- <font color=black size=4 face="微软雅黑">Page Cache特殊回收机制解决内存碎片问题
- 测试后在多线程环境下效率可达malloc的1.5倍