#include "PageCache.h"

PageCache PageCache::_inst;

//��ϵͳ����kҳ�ڴ�
Span* PageCache::NewSpan(size_t npage)
{
	std::unique_lock<std::mutex> lock(_mtx);

	if (npage >= NPAGES)//����128ҳ
	{
		void* ptr = SystemAlloc(NPAGES - 1);
		Span* span = new Span;

		span->_pageid = (PageID)ptr >> PAGE_SHIFT;//ҳ��
		span->_npage = npage;//ҳ��
		span->_objsize = npage << PAGE_SHIFT;//��С

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

	//��[npage + 1]...[NPAGES]ȥŲ
	for (size_t i = npage + 1; i < NPAGES; ++i)
	{
		SpanList* pagelist = &_pagelist[i];
		if (!pagelist->Empty())
		{
			Span* span = pagelist->PopFront();
			Span* splist = new Span;//ȱ�ݣ�����õ���new
			splist->_pageid = span->_pageid + span->_npage - npage;//ȥ��ȡ�ߵ���һ���֣���ʣ�µ�ҳ����span->_npage - npage�൱��һ��ƫ����
			splist->_npage = npage;
			span->_npage -= npage;
			_pagelist[span->_npage].PushFront(span);//�Ż�ʣ�µ�ҳ

			for (size_t i = 0; i < splist->_npage; ++i)
			{
				_id_span_map[splist->_pageid + i] = splist;//�ó��Ӻ���ȡ����ҳ
			}

			return splist;
		}
	}
	
	//128ҳ���ڴ�
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

//��ȡ�Ӷ���span��ӳ��
Span* PageCache::MapObjectToSpan(void* obj)
{
	PageID pageid = (PageID)obj >> PAGE_SHIFT;//��ַ��ҳ�ŵĹ�ϵ
	auto it = _id_span_map.find(pageid);
	assert(it != _id_span_map.end());

	return it->second;
}

//�ͷſ��ж���ص�PageCache,�����кϲ�
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	std::unique_lock<std::mutex> lock(_mtx);

	//����128ҳ��ֱ�Ӹ�ϵͳ
	if (span->_npage >= NPAGES)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
		return;
	}

	auto previt = _id_span_map.find(span->_pageid- 1);//ǰһ��ҳ��ID
	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		//���ǿ��У�����
		if (prevspan->_usecount != 0)
		{
			break;
		}
		//����ϲ�������NPAGESҳ��span���򲻺ϲ�,�����޷�����
		if ((prevspan->_npage + span->_npage) >= NPAGES)
		{
			break;
		}

		_pagelist[prevspan->_npage].Erase(prevspan);//ȡ��ǰһ��ҳ
		prevspan->_npage += span->_npage;
		delete span;
		span = prevspan;//�ϲ�ǰҳ�ɹ�

		previt = _id_span_map.find(span->_pageid - 1);//������ǰ�ϲ�
	}

	auto nextit = _id_span_map.find(span->_pageid + span->_npage);//��һ��span��id
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
