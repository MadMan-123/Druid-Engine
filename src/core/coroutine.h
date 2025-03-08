#pragma once
#include "../../include/druid.h"
#include <cstdio>
#include <setjmp.h>
#include <unistd.h>


typedef struct Coroutine {
	jmp_buf env;
	float waitTime;
	int active;
	struct Coroutine* next; // Linked list for active coroutines
} Coroutine;

static Coroutine* coroutineHead = NULL;

DAPI void addCoroutine(Coroutine* co);
DAPI void removeFinishedCoroutines();
DAPI void updateCoroutines(float dt);
DAPI void wait(Coroutine* co, float ms);
DAPI Coroutine* createCoroutine();


DAPI void startCoroutine(Coroutine* co, void (*func)(Coroutine* co));
