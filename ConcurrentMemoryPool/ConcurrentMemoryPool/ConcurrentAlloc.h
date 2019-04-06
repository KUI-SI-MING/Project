#pragma once

#include "ThreadCache.h"
#include "PageCache.h"

void* ConcurrentAlloc(size_t size)
{
	if (size > MAXBYTES)
	{
		size_t roundsize = ClassSize::Alignmentsize(size,  1 << PAGE_SHIFT);//对齐数 2^10 = 1K //?
		size_t npage = roundsize >> PAGE_SHIFT;//获取页数

		Span* span = PageCache::GetInstance()->NewSpan(npage);
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		
		return ptr;
	}
	else
	{
		//通过tsl，获取线程自己的Thread Cache
		if (tls_threadcache == nullptr)
		{
			tls_threadcache = new ThreadCache;
		}
		return tls_threadcache->Allocate(size);
	}
}

void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objsize;

	if (size > MAXBYTES)
	{
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
	}
	else
	{
		tls_threadcache->Deallocate(ptr, size);
	}
}