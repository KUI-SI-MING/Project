#pragma once

#include "ConcurrentMemoryPool.h"

class ThreadCache
{
public:
	//申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//从中心缓存获取对象
	void* ApplyFromCentralCache(size_t index, size_t size);

	//链表中的对象太多，开始回收
	void ListTooLong(FreeList* freelist, size_t byte);
private:
	FreeList _freelist[NLISTS];//自由链表
};

static _declspec(thread) ThreadCache* tls_threadcache = nullptr;//不是所有平台下都支持静态tls，可以设置动态tls