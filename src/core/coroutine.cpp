#include "coroutine.h"







void addCoroutine(Coroutine* co) 
{
	//set next coroutine to current head
	co->next = coroutineHead;
	//set head to current coroutine
	coroutineHead = co;
}

void removeFinishedCoroutines() 
{
	//set current to head
	Coroutine** current = &coroutineHead;
	//while current is not null
	while (*current) 
	{
		//if coroutine is not active
		if ((*current)->active == 0) 
		{
			//remove coroutine
			Coroutine* toRemove = *current;
			*current = (*current)->next;
			free(toRemove);
		} 
		else 
		{
			//move to next coroutine
			current = &(*current)->next;
		}
	}
}

void updateCoroutines(float dt) 
{
	//set current to head
	Coroutine* current = coroutineHead;
	//while current is not null
	while (current) 
	{
		//if coroutine is waiting
		if (current->waitTime > 0) 
		{
			//decrement wait time
			current->waitTime -= dt;
			//if wait time is less than or equal to 0 and coroutine is active
			if (current->waitTime <= 0 && current->active) 
			{
				// Resume coroutine
				longjmp(current->env, 1); 
			}
		}
		//move to next coroutine
		current = current->next;
	}
}

void wait(Coroutine* co, float ms)
{
	//set wait time
	co->waitTime = ms;
	//if coroutine is not active
	if (!setjmp(co->env)) return;
}

Coroutine* createCoroutine()
{
	//allocate coroutine
	Coroutine* co = (Coroutine*)malloc(sizeof(Coroutine));
	//add coroutine to linked list
	addCoroutine(co);

	return co;
}

void startCoroutine(Coroutine* co, void (*func)(Coroutine* co))
{
	if(co->active) return;
	//set active to 1
	co->active = 1;
	func(co);
}
