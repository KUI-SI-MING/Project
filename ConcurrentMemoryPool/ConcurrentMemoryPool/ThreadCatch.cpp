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
		freelist->SetMaxSize(fetchnum + 1);//��0��ʼ,num_to_move?
		//freelist->SetMaxSize(num_to_move + 1);//��0��ʼ,num_to_move?
	}

	return start;
}

//�������
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAXBYTES);
	
	//�����������ȵ�ȷ�������տ��Ʊ�����ѡ�񳤶�
	//����ȡ�� ���� + O(1)
	size_t actual_size = ClassSize::Alignup(size);//�����С
	size_t index = ClassSize::Index(actual_size);//λ����һ������

	FreeList* flist = &_freelist[index];
	if (flist->Empty())
	{
		return ApplyFromCentralCache(index, size);//����û�ж�����Central Cache��������
	}
	else
	{
		return flist->Pop();//���京�ж���
	}
}

//����̫��ʱ�ض���
void ThreadCache::ListTooLong(FreeList* freelist, size_t byte)
{
	void* start = freelist->Clear();
	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}

//�ͷŶ���
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(byte <= MAXBYTES);
	size_t index = ClassSize::Index(byte);
	FreeList* freelist = &_freelist[index];
	freelist->Push(ptr);
	
	//���������������������һ�����������Ļ����ƶ�������ʱ
	//��ʼ���ն������Ļ���
	if (freelist->Size() >= freelist->MaxSize())
	{
		ListTooLong(freelist, byte);
	}
}
