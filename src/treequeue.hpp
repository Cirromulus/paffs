/*
 * Copyright (c) 2016-2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2016-2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "stddef.h"

#define QUEUE_GROW_SIZE 10
    //Anzahl von Speicherplätzen, um die die Warteschlange bei Größenänderung wachsen soll. Die Implementation ist so angelegt, dass beim Verkleinern der Warteschlange die Anzahl der verfügbaren Speicherplätze ein ganzzahliges Vielfaches dieser Größe ist.
#define QUEUE_INIT_SIZE 10
    //Initiale Anzahl von Speicherplätzen.
#define QUEUE_SHRINK_AT 10
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
