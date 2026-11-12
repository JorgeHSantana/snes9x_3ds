#ifndef IBO_IDENTITY_H
#define IBO_IDENTITY_H

// The identity index list: entry k holds k.
//
// The shadow and glow passes draw a vertex range in order, indexed only
// because the frame must stay all DrawElements (the console dislikes
// DrawArrays mixed in). Each such draw used to copy "from, from+1, ..."
// into a scratch and flush it from the CPU cache with its own GSP call,
// in each eye. Any run of consecutive indices is a slice of the identity
// list, so the list is written and flushed once, at start, and a draw
// only points into it.

#define IBO_IDENTITY_ENTRIES 65536u   // every value a 16-bit index can hold

static inline void ibo_identity_fill(unsigned short *list, unsigned int entries)
{
    for (unsigned int k = 0; k < entries; k++)
        list[k] = (unsigned short)k;
}

// the indices of vertices from .. from+count-1, or a null pointer when the
// run does not fit the list (or there is no list): the caller skips the draw
static inline const unsigned short *ibo_identity_run(const unsigned short *list, unsigned int entries, int from, int count)
{
    if (list == 0 || from < 0 || count <= 0)
        return 0;
    if ((unsigned int)from > entries || (unsigned int)count > entries - (unsigned int)from)
        return 0;
    return list + from;
}

#endif
