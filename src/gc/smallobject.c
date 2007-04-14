/*
Copyright (C) 2001-2006, The Perl Foundation.
$Id$

=head1 NAME

src/resources.c - Handling Small Object Pools

=head1 DESCRIPTION

Handles the accessing of small object pools (header pools).

=head2 Functions

=over 4

=cut

*/

#include "parrot/parrot.h"
#include <assert.h>

#define GC_DEBUG_REPLENISH_LEVEL_FACTOR 0.0
#define GC_DEBUG_UNITS_PER_ALLOC_GROWTH_FACTOR 1
#define REPLENISH_LEVEL_FACTOR 0.3
/* this factor is totally arbitrary, but gives good timings for stress.pasm */
#define UNITS_PER_ALLOC_GROWTH_FACTOR 1.75

#define POOL_MAX_BYTES 65536*128

/*

=item C<INTVAL
contained_in_pool(Interp *interp, Small_Object_Pool *pool, void *ptr)>

Returns whether C<pool> contains C<*ptr>.

=cut

*/

INTVAL
contained_in_pool(Interp *interp, Small_Object_Pool *pool, void *ptr)
{
    Small_Object_Arena *arena;

    ptr = PObj_to_ARENA(ptr);

    for (arena = pool->last_Arena; arena; arena = arena->prev) {
        const ptrdiff_t ptr_diff =
            (ptrdiff_t)ptr - (ptrdiff_t)arena->start_objects;

        if (0 <= ptr_diff
                && ptr_diff <
                (ptrdiff_t)(arena->used * pool->object_size)
                && ptr_diff % pool->object_size == 0)
            return 1;
    }
    return 0;
}

/*

=item C<int
Parrot_is_const_pmc(Parrot_Interp interp, PMC *pmc)>

Returns whether C<*pmc> is a constant PMC.

=cut

*/

int
Parrot_is_const_pmc(Parrot_Interp interp, PMC *pmc)
{
    Small_Object_Pool * const pool = interp->arena_base->constant_pmc_pool;
    int c;
    c = contained_in_pool(interp, pool, pmc);

    /* some paranoia first */
    assert(!!PObj_constant_TEST(pmc) == !!c);
    return c;
}


/*

=item C<void
more_traceable_objects(Interp *interp, Small_Object_Pool *pool)>

We're out of traceable objects. Try a DOD, then get some more if needed.

=cut

*/

static void
more_traceable_objects(Interp *interp, Small_Object_Pool *pool)
{
    if (pool->skip)
        pool->skip = 0;
    else {
        Small_Object_Arena * const arena = pool->last_Arena;
        if (arena) {
            if (arena->used == arena->total_objects)
                Parrot_do_dod_run(interp, DOD_trace_stack_FLAG);
            if (pool->num_free_objects <= pool->replenish_level)
                pool->skip = 1;
        }
    }

    /* requires that num_free_objects be updated in Parrot_do_dod_run. If dod
     * is disabled, then we must check the free list directly. */
    if (!pool->free_list) {
        (*pool->alloc_objects) (interp, pool);
    }
}


/*

=item C<static void
gc_ms_add_free_object(Interp *interp, Small_Object_Pool *pool, void *to_add)>

Add an unused object back to the free pool for later reuse.

=item C<static void *
gc_ms_get_free_object(Interp *interp, Small_Object_Pool *pool)>

Get a new object from the free pool and return it.

=cut

*/

static void
gc_ms_add_free_object(Interp *interp, Small_Object_Pool *pool, void *to_add)
{
    *(void **)to_add = pool->free_list;
    pool->free_list = to_add;
}


static void *
gc_ms_get_free_object(Interp *interp, Small_Object_Pool *pool)
{
    void *ptr;

    /* if we don't have any objects */
    if (!pool->free_list)
        (*pool->more_objects)(interp, pool);
    ptr = pool->free_list;
    pool->free_list = *(void **)ptr;
    PObj_flags_SETTO((PObj*) ptr, 0);
    --pool->num_free_objects;
    return ptr;
}

/*

=item C< void
Parrot_add_to_free_list(Interp *interp,
        Small_Object_Pool *pool,
        Small_Object_Arena *arena,
        UINTVAL start,
        UINTVAL end)>

Adds the memory between C<start> and C<end> to the free list.

=cut

*/

void
Parrot_add_to_free_list(Interp *interp,
        Small_Object_Pool *pool,
        Small_Object_Arena *arena,
        UINTVAL start,
        UINTVAL end)
{
    UINTVAL i;
    void *object;

    pool->total_objects += end - start;
    arena->used = end;

    /* Move all the new objects into the free list */
    object = (void *)((char *)arena->start_objects +
            start * pool->object_size);
    for (i = start; i < end; i++) {
        PObj_flags_SETTO((PObj *)object, PObj_on_free_list_FLAG);
        /*
         * during GC buflen is used to check for objects on the
         * free_list
         */
        PObj_buflen((PObj*)object) = 0;
        pool->add_free_object(interp, pool, object);
        object = (void *)((char *)object + pool->object_size);
    }
    pool->num_free_objects += end - start;
}

/*
 * insert the new arena into the pool's structure, update stats
 */
void
Parrot_append_arena_in_pool(Interp *interp, Small_Object_Pool *pool,
    Small_Object_Arena *new_arena, size_t size)
{

    /* Maintain the *_arena_memory invariant for stack walking code. Set it
     * regardless if we're the first pool to be added. */
    if (!pool->last_Arena
            || (pool->start_arena_memory > (size_t)new_arena->start_objects))
        pool->start_arena_memory = (size_t)new_arena->start_objects;

    if (!pool->last_Arena || (pool->end_arena_memory <
                (size_t)new_arena->start_objects + size))
        pool->end_arena_memory = (size_t)new_arena->start_objects + size;
    new_arena->total_objects = pool->objects_per_alloc;
    new_arena->next = NULL;
    new_arena->prev = pool->last_Arena;
    if (new_arena->prev) {
        new_arena->prev->next = new_arena;
    }
    pool->last_Arena = new_arena;
    interp->arena_base->header_allocs_since_last_collect++;
}

/*

=item C<static void
gc_ms_alloc_objects(Interp *interp, Small_Object_Pool *pool)>

We have no more headers on the free header pool. Go allocate more
and put them on.

=cut

*/

static void
gc_ms_alloc_objects(Interp *interp, Small_Object_Pool *pool)
{
    Small_Object_Arena *new_arena;
    size_t size;
    UINTVAL start, end;

    /* Setup memory for the new objects */
    new_arena = (Small_Object_Arena *)mem_internal_allocate(
        sizeof (Small_Object_Arena));
    if (!new_arena)
        PANIC("Out of arena memory");
    size = pool->object_size * pool->objects_per_alloc;
    /* could be mem_internal_allocate too, but calloc is fast */
    new_arena->start_objects = mem_internal_allocate_zeroed(size);

    Parrot_append_arena_in_pool(interp, pool, new_arena, size);

    start = 0;
    end = pool->objects_per_alloc;
    Parrot_add_to_free_list(interp, pool, new_arena, start, end);

    /* Allocate more next time */
    if (GC_DEBUG(interp)) {
        pool->objects_per_alloc *= GC_DEBUG_UNITS_PER_ALLOC_GROWTH_FACTOR;
        pool->replenish_level =
                (size_t)(pool->total_objects *
                GC_DEBUG_REPLENISH_LEVEL_FACTOR);
    }
    else {
        pool->objects_per_alloc = (size_t)(pool->objects_per_alloc *
            UNITS_PER_ALLOC_GROWTH_FACTOR);
        pool->replenish_level =
                (size_t)(pool->total_objects * REPLENISH_LEVEL_FACTOR);
    }
    /* check alloc size against maximum */
    size = pool->object_size * pool->objects_per_alloc;

    if (size > POOL_MAX_BYTES) {
        pool->objects_per_alloc = POOL_MAX_BYTES / pool->object_size;
    }
}

/*

=item C<Small_Object_Pool *
new_small_object_pool(Interp *interp,
        size_t object_size, size_t objects_per_alloc)>

Creates a new C<Small_Object_Pool> and returns a pointer to it.

=cut

*/


Small_Object_Pool *
new_small_object_pool(Interp *interp,
        size_t object_size, size_t objects_per_alloc)
{
    Small_Object_Pool * const pool =
        (Small_Object_Pool * const)mem_internal_allocate_zeroed(
            sizeof (Small_Object_Pool));

    SET_NULL(pool->last_Arena);
    SET_NULL(pool->free_list);
    SET_NULL(pool->mem_pool);

    pool->object_size       = object_size;
    pool->objects_per_alloc = objects_per_alloc;

    return pool;
}

void
gc_pmc_ext_pool_init(Interp *interp, Small_Object_Pool *pool)
{
    pool->add_free_object = gc_ms_add_free_object;
    pool->get_free_object = gc_ms_get_free_object;
    pool->alloc_objects   = gc_ms_alloc_objects;
    pool->more_objects    = gc_ms_alloc_objects;
}

static void
gc_ms_pool_init(Interp *interp, Small_Object_Pool *pool)
{
    pool->add_free_object = gc_ms_add_free_object;
    pool->get_free_object = gc_ms_get_free_object;
    pool->alloc_objects   = gc_ms_alloc_objects;
    pool->more_objects = more_traceable_objects;
}

/*

=item C<void Parrot_gc_ms_init(Interp *interp)>

Initialize the state structures of the gc system. Called immediately before
creation of memory pools. This function must set the function pointers
for C<add_free_object_fn>, C<get_free_object_fn>, C<alloc_object_fn>, and
C<more_object_fn>.

=cut

*/

void
Parrot_gc_ms_init(Interp *interp)
{
    Arenas * const arena_base = interp->arena_base;

    arena_base->do_dod_run = Parrot_dod_ms_run;
    arena_base->de_init_gc_system = (void (*)(Interp*)) NULLfunc;
    arena_base->init_pool = gc_ms_pool_init;
}

/*

=item C<void
Parrot_small_object_pool_merge(Interp *interp,
            Small_Object_Pool *dest, Small_Object_Pool *source)>

Merge C<source> into C<dest>.

=cut

*/

void
Parrot_small_object_pool_merge(Interp *interp,
        Small_Object_Pool *dest, Small_Object_Pool *source) {
    Small_Object_Arena *cur_arena;
    Small_Object_Arena *next_arena;
    void **free_list_end;

    /* XXX num_free_objects doesn't seem to be accounted correctly in, e.g.,
     * the PMC_EXT pool.
     */
#if 0
    if (source->num_free_objects == source->total_objects) {
        return;
    }
#endif

    /* assert(source->total_objects); */
    assert(dest->object_size == source->object_size);
    assert((dest->name == NULL && source->name == NULL) ||
        0 == strcmp(dest->name, source->name));

    dest->total_objects += source->total_objects;

    /* append new free_list to old */
    /* XXX this won't work with, e.g., gc_gms */
    free_list_end = &dest->free_list;
    while (*free_list_end) {
        free_list_end = (void **)*free_list_end;
    }
    *free_list_end = source->free_list;

    /* now append source arenas */
    cur_arena = source->last_Arena;
    while (cur_arena) {
        size_t total_objects;
        next_arena = cur_arena->prev;
        cur_arena->next = cur_arena->prev = NULL;

        total_objects = cur_arena->total_objects;

        Parrot_append_arena_in_pool(interp, dest, cur_arena,
            cur_arena->total_objects);

        cur_arena->total_objects = total_objects; /* XXX needed? */

        cur_arena = next_arena;
    }

    /* remove things from source */
    /* XXX is this enough? */
    source->last_Arena = NULL;
    source->free_list = NULL;
    source->total_objects = 0;
    source->num_free_objects = 0;

}
/*

=back

=head1 SEE ALSO

F<include/parrot/smallobject.h>, F<docs/memory_internals.pod>.

=cut

*/


/*
 * Local variables:
 *   c-file-style: "parrot"
 * End:
 * vim: expandtab shiftwidth=4:
 */
