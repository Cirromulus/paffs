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

#include "treequeue.hpp"
#include <stdlib.h>
#include <string.h>

#define SUCCESS 0
#define ERR_INVAL 1
#define ERR_NOMEM 2

#define FALSE 0
#define TRUE 1

int
queueDestroy(TreeQueue* queue)
{
    if (queue == NULL)
    {
        return ERR_INVAL;
    }
    free(queue->data);
    free(queue);
    return SUCCESS;
}

int
queueEmpty(TreeQueue* queue)
{
    if (queue == NULL || queue->front == queue->back)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

TreeQueue*
queueNew(void)
{
    TreeQueue* queue = static_cast<TreeQueue*>(malloc(sizeof(*queue)));
    if (queue == NULL)
    {
        return NULL;
    }
    if ((queue->data = static_cast<void**>(malloc(QUEUE_INIT_SIZE * sizeof(*(queue->data)))))
        == NULL)
    {
        free(queue);
        return NULL;
    }
    queue->front = queue->back = 0;
    queue->size = QUEUE_INIT_SIZE;
    return queue;
}

void*
queueDequeue(TreeQueue* queue)
{
    if (queue == NULL || queue->front == queue->back)
    {
        return NULL;
    }
    void* data = queue->data[queue->front];
    queue->front = (queue->front + 1) % queue->size;
    size_t len = ((queue->back < queue->front) ? (queue->back + queue->size) : queue->back)
                 - queue->front;
    if (queue->size > QUEUE_SHRINK_AT && len <= queue->size - QUEUE_SHRINK_AT)
    {
        size_t new_size = len + ((len + QUEUE_GROW_SIZE) % QUEUE_GROW_SIZE);
        if (queue->front < queue->back)
        {
            if (queue->back > new_size)
            {
                size_t off = queue->back - new_size;
                memcpy(queue->data, queue->data + new_size, off * sizeof(*(queue->data)));
                if (queue->front > new_size)
                {
                    queue->front = off - len;
                }
                queue->back = off;
            }
            else if (queue->back == new_size)
            {
                queue->back = 0;
            }
        }
        else
        {
            size_t off = queue->size - queue->front;
            memmove(queue->data + (new_size - off),
                    queue->data + queue->front,
                    off * sizeof(*(queue->data)));
            queue->front = new_size - off;
        }
        queue->data = static_cast<void**>(realloc(queue->data, new_size * sizeof(*(queue->data))));
        queue->size = new_size;
    }
    return data;
}

int
queue_enqueue(TreeQueue* queue, void* data)
{
    if (queue == NULL)
    {
        return ERR_INVAL;
    }
    queue->data[queue->back] = data;
    queue->back = (queue->back + 1) % queue->size;
    if (queue->back == queue->front)
    {
        size_t new_size = queue->size + QUEUE_GROW_SIZE;
        void** new_data =
                static_cast<void**>(realloc(queue->data, new_size * sizeof(*(queue->data))));
        if (new_data == NULL)
        {
            queue->back = (queue->size + queue->back - 1) % queue->size;
            return ERR_NOMEM;
        }
        if (queue->back != 0)
        {
            if (queue->back < QUEUE_GROW_SIZE)
            {
                memcpy(queue->data + queue->size,
                       queue->data,
                       queue->back * sizeof(*(queue->data)));
                queue->back += queue->size - 1;
            }
            else
            {
                size_t rem = queue->back - QUEUE_GROW_SIZE;
                memcpy(queue->data + queue->size,
                       queue->data,
                       QUEUE_GROW_SIZE * sizeof(*(queue->data)));
                memcpy(queue->data, queue->data + QUEUE_GROW_SIZE, rem * sizeof(*(queue->data)));
                queue->back = rem;
            }
        }
        else
        {
            queue->back = queue->size;
        }
        queue->data = new_data;
        queue->size = new_size;
    }
    return SUCCESS;
}
