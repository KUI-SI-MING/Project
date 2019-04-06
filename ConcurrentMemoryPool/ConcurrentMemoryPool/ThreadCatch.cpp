#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::ApplyFromCentralCache(size_t index, size_t byte)
{
	FreeList* freelist = &_freelist[index];
	size_t num_to_move = min(ClassSize::NumMoveSize(byte), freelist->MaxSize());

	void* start, *end;
	size_t fetchnum = CentralCache::GetInstance()->DivideRangeObj(start, end, num_to_move, byte);

	if (fetchnum > 1)
		freelist->PushRange(NEXT_OBJ(start), end, fetchnum - 1);//?

	if (num_to_move == freelist->MaxSize())
	{
		freelist->SetMaxSize(fetchnum + 1);//从0开始,num_to_move?
		//freelist->SetMaxSize(num_to_move + 1);//从0开始,num_to_move?
	}

	return start;
}

//申请对象
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAXBYTES);
	
	//①自由链表长度的确定：按照控制比率来选择长度
	//对齐取整 无锁 + O(1)
	size_t actual_size = ClassSize::Alignup(size);//对齐大小
	size_t index = ClassSize::Index(actual_size);//位于哪一个区间

	FreeList* flist = &_freelist[index];
	if (flist->Empty())
	{
		return ApplyFromCentralCache(index, size);//区间没有对象，向Central Cache进行申请
	}
	else
	{
		return flist->Pop();//区间含有对象
	}
}

//链表太长时回对象
void ThreadCache::ListTooLong(FreeList* freelist, size_t byte)
{
	void* start = freelist->Clear();
	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}

//释放对象
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(byte <= MAXBYTES);
	size_t index = ClassSize::Index(byte);
	FreeList* freelist = &_freelist[index];
	freelist->Push(ptr);
	
	//当自由链表对象数量超过一次批量从中心缓存移动的数量时
	//开始回收对象到中心缓存
	if (freelist->Size() >= freelist->MaxSize())
	{
		ListTooLong(freelist, byte);
	}
}
