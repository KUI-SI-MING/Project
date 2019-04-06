#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

//从Page Cache获取一个span
Span* CentralCache::GetOneSpan(SpanList* spanlist, size_t bytes)
{
	Span* span = spanlist->Begin();
	while (span != spanlist->End())
	{
		if (span->_objlist != nullptr)//当前span有对象
			return span;

		span = span->_next;
	}

	//向pagecache申请一个新的合适大小的span
	size_t npage = ClassSize::NumMovePage(bytes);//获取页数
	Span* newspan = PageCache::GetInstance()->NewSpan(npage);//根据页数申清span

	//将span的内存切割成一个个bytes大小的对象挂起来
	char* start = (char*)(newspan->_pageid << PAGE_SHIFT);
	char* end = start + (newspan->_npage << PAGE_SHIFT);
	char* cur = start;
	char* next = cur + bytes;

	while (next < end)
	{
		NEXT_OBJ(cur) = next;
		cur = next;
		next = cur + bytes;
	}
	NEXT_OBJ(cur) = nullptr;
	newspan->_objlist = start;
	newspan->_objsize = bytes;
	newspan->_usecount = 0;

	//将newsapn插入到spanlist
	spanlist->PushFront(newspan);
	return newspan;
}

//将一定数量的对象分配给thread cache
size_t CentralCache::DivideRangeObj(void*& start, void*& end, size_t num, size_t bytes)
{
	size_t index = ClassSize::Index(bytes);
	SpanList* spanlist = &_spanList[index];

	//对桶加锁
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	Span* span = GetOneSpan(spanlist, bytes);
	void* cur = span->_objlist;
	void* prev = cur;
	size_t realnum = 0;

	while (cur != nullptr && realnum < num)
	{
		prev = cur;
		cur = NEXT_OBJ(cur);
		++realnum;
	}

	start = span->_objlist;
	end = prev;
	NEXT_OBJ(end) = nullptr;

	span->_objlist = cur;
	span->_usecount += realnum;
	
	//当一个span为空，将span移动到尾上
	if (span->_objlist == nullptr)
	{
		spanlist->Erase(span);
		spanlist->PushBack(span);
	}

	return realnum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
	size_t index = ClassSize::Index(byte);
	SpanList* spanlist = &_spanList[index];

	std::unique_lock<std::mutex> lock(spanlist->_mtx);//原子操作
	while (start)
	{
		void *next = NEXT_OBJ(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);//得到具体的span

		//当释放对象回到空的span，把span移到尾上，提高效率
		if (span->_objlist == nullptr)
		{
			spanlist->Erase(span);
			spanlist->PushBack(span);
		}

		//并入对象到span
		NEXT_OBJ(start) = span->_objlist;
		span->_objlist = start;

		//usecount==0表示span切出去的对象都回来了
		//释放span回到pagescache进行合并
		if (--span->_usecount == 0)
		{
			spanlist->Erase(span);

			span->_objlist = nullptr;
			span->_objlist = 0;
			span->_prev = nullptr;
			span->_next = nullptr;

			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		start = next;
	}
}