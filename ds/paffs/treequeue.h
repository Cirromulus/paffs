/**
 * Treequeue, currently just used for printig out whole tree
 */
#include "stddef.h"

#define QUEUE_GROW_SIZE 20
    //Anzahl von Speicherplätzen, um die die Warteschlange bei Größenänderung wachsen soll. Die Implementation ist so angelegt, dass beim Verkleinern der Warteschlange die Anzahl der verfügbaren Speicherplätze ein ganzzahliges Vielfaches dieser Größe ist.
#define QUEUE_INIT_SIZE 10
    //Initiale Anzahl von Speicherplätzen.
#define QUEUE_SHRINK_AT 5
	//Die Anzahl freier Speicherplätze, die mindestens vorhanden sein müssen, um die Warteschlange zu verkleinern.


typedef struct queue_s {
	void **data;
	size_t front;
	size_t back;
	size_t size;
} queue_s;

int queue_destroy(queue_s *queue);
int queue_empty(queue_s *queue);
queue_s *queue_new(void);
void *queue_dequeue(queue_s *queue);
int queue_enqueue(queue_s *queue, void *data);
