#include "simple_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
* I need a queue to hold u16s. This code does that
*/

void SimpleQueue_Init(SimpleQueue_t *q, uint16_t *buffer, uint16_t size)
{
  q->headIdx = 0;
  q->tailIdx = 0;
  q->currLen = 0;
  q->maxLen = size;
  q->buf = buffer;
  memset((void *) buffer, 0x00, sizeof(uint16_t) * size);
}

void SimpleQueue_Deinit(SimpleQueue_t *q)
{
  memset((void *) q->buf, 0x00, sizeof(uint16_t) * q->maxLen);
}

int SimpleQueue_Push(SimpleQueue_t *q, uint16_t data)
{
  if (q->currLen == q->maxLen)
  {
    printf("Queue full!\n");
    return 1;
  }
  q->buf[q->tailIdx] = data;
  q->tailIdx = (q->tailIdx + 1) % q->maxLen;
  q->currLen++;
  return 0;
}

int SimpleQueue_Pop(SimpleQueue_t *q, uint16_t *data)
{
  if (q->currLen == 0)
  {
    printf("Queue empty!\n");
    return 1;
  }
  *data = q->buf[q->headIdx];
  q->currLen--;
  q->headIdx = (q->headIdx + 1) % q->maxLen;
  return 0;
}

int SimpleQueue_Seek(SimpleQueue_t *q, uint16_t idx, uint16_t *data)
{
  if (idx > q->currLen)
  {
    printf("Out of bounds\n");
    return 1;
  }
  *data = q->buf[(q->headIdx + idx) % q->maxLen];
  return 0;
}

void SimpleQueue_PrintQueue(SimpleQueue_t *q)
{
  printf("Head Idx %d, Tail Idx %d, curr len %d, max len %d\n", q->headIdx, q->tailIdx, q->currLen, q->maxLen);
  for (int i = 0; i < q->currLen; i++)
  {
    uint16_t data = 0;
    SimpleQueue_Seek(q, i, &data);
    printf("%d:%d\n", i, data);
  }
}

#if 0 // TESTER
int main()
{
  SimpleQueue_t q;
  uint16_t buffer[16];
  SimpleQueue_Init(&q, (uint16_t *) &buffer, 4);

  SimpleQueue_Push(&q, 10);
  SimpleQueue_Push(&q, 20);
  SimpleQueue_Push(&q, 30);

  SimpleQueue_PrintQueue(&q);

  printf("Pop 1\n");
  uint16_t data = 0;
  SimpleQueue_Pop(&q, &data);
  printf("Data %d\n", data);
  SimpleQueue_PrintQueue(&q);

  printf("Push 2\n");
  SimpleQueue_Push(&q, 40);
  SimpleQueue_Push(&q, 50);
  SimpleQueue_PrintQueue(&q);

  printf("Full?\n");
  SimpleQueue_Push(&q, 60);
  SimpleQueue_Pop(&q, &data);
  printf("Data %d\n", data);
  SimpleQueue_Push(&q, 60);
  SimpleQueue_PrintQueue(&q);

  return 0;
}
#endif
