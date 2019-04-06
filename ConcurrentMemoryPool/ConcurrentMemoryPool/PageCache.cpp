#include "PageCache.h"

PageCache PageCache::_inst;

//向系统申请k页内存
Span* PageCache::NewSpan(size_t npage)
{
	std::unique_lock<std::mutex> lock(_mtx);

	if (npage >= NPAGES)//超过128页
	{
		void* ptr = SystemAlloc(NPAGES - 1);
		Span* span = new Span;

		span->_pageid = (PageID)ptr >> PAGE_SHIFT;//页号
		span->_npage = npage;//页数
		span->_objsize = npage << PAGE_SHIFT;//大小

		_id_span_map[span->_pageid] = span;
		return span;
	}

	Span* span = _NewSpan(npage);
	span->_objsize = npage << PAGE_SHIFT;
	return span;
}

Span* PageCache::_NewSpan(size_t npage)
{
	
	if (!_pagelist[npage].Empty())
	{
		return _pagelist[npage].PopFront();
	}

	//从[npage + 1]...[NPAGES]去挪
	for (size_t i = npage + 1; i < NPAGES; ++i)
	{
		SpanList* pagelist = &_pagelist[i];
		if (!pagelist->Empty())
		{
			Span* span = pagelist->PopFront();
			Span* splist = new Span;//缺陷：这儿用到了new
			splist->_pageid = span->_pageid + span->_npage - npage;//去掉取走的那一部分，看剩下的页数，span->_npage - npage相当于一个偏移量
			splist->_npage = npage;
			span->_npage -= npage;
			_pagelist[span->_npage].PushFront(span);//放回剩下的页

			for (size_t i = 0; i < splist->_npage; ++i)
			{
				_id_span_map[splist->_pageid + i] = splist;//拿出从后面取到的页
			}

			return splist;
		}
	}
	
	//128页的内存
	void* ptr = SystemAlloc(NPAGES - 1);

	Span* largespan = new Span;
	largespan->_pageid = (PageID)ptr >> PAGE_SHIFT;
	largespan->_npage = NPAGES - 1;
	_pagelist[NPAGES - 1].PushFront(largespan);

	for (size_t i = 0; i < largespan->_npage; ++i)
	{
		_id_span_map[largespan->_npage + i] = largespan;
	}
	return _NewSpan(npage);
}

//获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj)
{
	PageID pageid = (PageID)obj >> PAGE_SHIFT;//地址与页号的关系
	auto it = _id_span_map.find(pageid);
	assert(it != _id_span_map.end());

	return it->second;
}

//释放空闲对象回到PageCache,并进行合并
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	std::unique_lock<std::mutex> lock(_mtx);

	//大于128页则直接给系统
	if (span->_npage >= NPAGES)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
		return;
	}

	auto previt = _id_span_map.find(span->_pageid- 1);//前一个页的ID
	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		//不是空闲，跳出
		if (prevspan->_usecount != 0)
		{
			break;
		}
		//如果合并出超过NPAGES页的span，则不合并,否则无法管理
		if ((prevspan->_npage + span->_npage) >= NPAGES)
		{
			break;
		}

		_pagelist[prevspan->_npage].Erase(prevspan);//取出前一个页
		prevspan->_npage += span->_npage;
		delete span;
		span = prevspan;//合并前页成功

		previt = _id_span_map.find(span->_pageid - 1);//继续向前合并
	}

	auto nextit = _id_span_map.find(span->_pageid + span->_npage);//下一个span的id
	while (nextit != _id_span_map.end())
	{
		Span* nextspan = nextit->second;
		if (nextspan->_usecount != 0)
		{
			break;
		}
		if (span->_npage + nextspan->_npage >= NPAGES)
		{
			break;
		}

		_pagelist[nextspan->_npage].Erase(nextspan);
		span->_npage += nextspan->_npage;
		delete nextspan;

		nextit = _id_span_map.find(span->_pageid + span->_npage);

	}

	for (size_t i = 0; i < span->_npage; ++i)
	{
		_id_span_map[span->_pageid + i] = span;
	}

	_pagelist[span->_npage].PushFront(span);
}
