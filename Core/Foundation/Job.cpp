#include "stdafx.h"
#include "Job.h"

void Core::WorkQueue::Add(const Job* job)  
{  
   if (length == 0)  
   {  
       first = const_cast<Job*>(job);  
       last = const_cast<Job*>(job);  
   }  
   else  
   {  
       last->next = const_cast<Job*>(job);  
       last = const_cast<Job*>(job);  
   }  

   length += 1;  
}

Core::Job* Core::WorkQueue::GetNext()
{
	if (length == 0)
		return nullptr;

	Job* current = first;
	while (current)
	{
		if (current->status == JobStatus::PENDING)
		{
			return current;
		}

		current = current->next;
	}

	return nullptr;
}

void Core::WorkQueue::Clear()
{
	if (length == 0)
		return;

	Job* current = first;
	while (current)
	{
		Job* previous = current;
		current = current->next;
		delete(previous);
	}

	length = 0;
	first = nullptr;
	last = nullptr;
}

bool Core::WorkQueue::Done()
{
	if (length == 0)
		return true;

	Job* current = first;
	while (current)
	{
		if (current->status != JobStatus::COMPLETE)
			return false;

		current = current->next;
	}

	return true;
}
