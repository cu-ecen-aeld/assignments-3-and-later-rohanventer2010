/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer, size_t char_offset, size_t *entry_offset_byte_rtn)
{
    /* Check if the buffer is NULL */
    if (buffer == NULL) {
        return NULL;
    }

    /* Check if the buffer is empty */
    if (!buffer->full && buffer->in_offs == buffer->out_offs) {
        return NULL;
    }

    uint8_t index = buffer->out_offs;;
    size_t char_offset_int = char_offset;
    bool done = false;

    while (!done)
    {
        if (char_offset_int < buffer->entry[index].size)
        {
            // the value is in this entry
            *entry_offset_byte_rtn = char_offset_int;
            return &(buffer->entry[index]);
        }

        /* decrease the internal offset */
        char_offset_int -= buffer->entry[index].size;

        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        if (index == buffer->in_offs)
            done = true;
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* return a pointer to the entry if an entry was overwritten so the caller can free the memory
*/
const struct aesd_buffer_entry *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /* Check if the buffer is NULL */
    if (buffer == NULL)
        return NULL;

    /* Check if the add_entry is NULL */
    if (add_entry == NULL)
        return NULL;

    /* save pointer to the old entry */
    /* this should be NULL if it has not been written before */
    const struct aesd_buffer_entry *old_entry = &buffer->entry[buffer->in_offs];

    /* add new entry */
    buffer->entry[buffer->in_offs] = *add_entry;

    /* update write pointer and wrap around */
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    /* check if buffer is full, and advance the read pointer and wrap around */
    if (buffer->full)
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    /* update the full flag */
    buffer->full = (buffer->in_offs == buffer->out_offs);

    return old_entry;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
