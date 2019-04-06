#pragma once
#include "ConcurrentMemoryPool.h"

class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_inst;
	}

	//从Page Cache获取一个span
	Span* GetOneSpan(SpanList* spanlist, size_t bytes);

	//将一定数量的对象分配给thread cache
	size_t DivideRangeObj(void*& start, void*& end, size_t n, size_t bytes);

	//将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t byte_size);

private:
	//中心缓存自由链表
	SpanList _spanList[NLISTS];
private:
	CentralCache() = default;
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;

	//单例对象
	static CentralCache _inst;
};