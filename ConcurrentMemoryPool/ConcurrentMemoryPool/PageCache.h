#pragma once

#include "ConcurrentMemoryPool.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_inst;
	}

	Span* NewSpan(size_t npage);
	Span* _NewSpan(size_t npage);

	//获取对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//释放空闲span回到PageCache, 并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

private:
	SpanList _pagelist[NPAGES];

private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	static PageCache _inst;

	
	std::mutex _mtx;
	//std::unordered_map<PageID, Span*> _id_span_map;
	std::map<PageID, Span*> _id_span_map;
};