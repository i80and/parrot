/*
Copyright (C) 2001-2003, The Perl Foundation.
$Id$

=head1 NAME

src/spf_vtable.c - Parrot sprintf

=head1 DESCRIPTION

Implements the two families of functions C<Parrot_sprintf> may use to
retrieve arguments.

=head2 Var args Functions

*/

#define IN_SPF_SYSTEM

#include "parrot/parrot.h"

#include <stdarg.h>
#include "spf_vtable.str"

/* HEADERIZER HFILE: none */

/* HEADERIZER BEGIN: static */

static STRING * getchr_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static STRING * getchr_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static HUGEFLOATVAL getfloat_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static HUGEFLOATVAL getfloat_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static HUGEINTVAL getint_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static HUGEINTVAL getint_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static void * getptr_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static void * getptr_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static STRING * getstring_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static STRING * getstring_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static UHUGEINTVAL getuint_pmc( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

static UHUGEINTVAL getuint_va( Interp *interp /*NN*/,
    INTVAL size,
    SPRINTF_OBJ *obj /*NN*/ )
        __attribute__nonnull__(1)
        __attribute__nonnull__(3);

/* HEADERIZER END: static */

/*

FUNCDOC: getchr_va

Gets a C<char> out of the C<va_list> in C<obj> and returns it as a
Parrot C<STRING>.

C<size> is unused.

*/

static STRING *
getchr_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list *arg = (va_list *)(obj->data);

    /* char promoted to int */
    char ch = (char)va_arg(*arg, int);

    return string_make(interp, &ch, 1, "iso-8859-1", 0);
}

/*

FUNCDOC: getint_va

Gets an integer out of the C<va_list> in C<obj> and returns it as a
Parrot C<STRING>.

C<size> is an C<enum spf_type_t> value which indicates the storage type
of the integer.

*/

static HUGEINTVAL
getint_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list * const arg = (va_list *)(obj->data);

    switch (size) {
    case SIZE_REG:
        return va_arg(*arg, int);

    case SIZE_SHORT:
        /* "'short int' is promoted to 'int' when passed through '...'" */
        return (short)va_arg(*arg, int);

    case SIZE_LONG:
        return va_arg(*arg, long);

    case SIZE_HUGE:
        return va_arg(*arg, HUGEINTVAL);

    case SIZE_XVAL:
        return va_arg(*arg, INTVAL);

    case SIZE_OPCODE:
        return va_arg(*arg, opcode_t);

    case SIZE_PMC:{
            PMC * const pmc = (PMC *)va_arg(*arg, PMC *);
            return VTABLE_get_integer(interp, pmc);
        }
    default:
        PANIC(interp, "Invalid int type!");
        return 0;
    }
}

/*

FUNCDOC: getuint_va

Gets an unsigned integer out of the C<va_list> in C<obj> and returns it
as a Parrot C<STRING>.

C<size> is an C<enum spf_type_t> value which indicates the storage type
of the integer.

*/

static UHUGEINTVAL
getuint_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list * const arg = (va_list *)(obj->data);

    switch (size) {
    case SIZE_REG:
        return va_arg(*arg, unsigned int);

    case SIZE_SHORT:
        /* short int promoted HLAGHLAGHLAGH. See note above */
        return (unsigned short)va_arg(*arg, unsigned int);

    case SIZE_LONG:
        return va_arg(*arg, unsigned long);

    case SIZE_HUGE:
        return va_arg(*arg, UHUGEINTVAL);

    case SIZE_XVAL:
        return va_arg(*arg, UINTVAL);

    case SIZE_OPCODE:
        return va_arg(*arg, opcode_t);

    case SIZE_PMC:{
            PMC *pmc = va_arg(*arg, PMC *);
            return (UINTVAL)VTABLE_get_integer(interp, pmc);
        }
    default:
        PANIC(interp, "Invalid uint type!");
        return 0;
    }
}

/*

FUNCDOC: getfloat_va

Gets an floating-point number out of the C<va_list> in C<obj> and
returns it as a Parrot C<STRING>.

C<size> is an C<enum spf_type_t> value which indicates the storage type of
the number.

*/

static HUGEFLOATVAL
getfloat_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list * const arg = (va_list *)(obj->data);

    switch (size) {
    case SIZE_SHORT:
        /* float is promoted to double */
        return (HUGEFLOATVAL)(float)va_arg(*arg, double);

    case SIZE_REG:
        return (HUGEFLOATVAL)(double)va_arg(*arg, double);

    case SIZE_HUGE:
        return (HUGEFLOATVAL)(HUGEFLOATVAL)
                va_arg(*arg, HUGEFLOATVAL);

    case SIZE_XVAL:
        return (HUGEFLOATVAL)(FLOATVAL)
                va_arg(*arg, FLOATVAL);

    case SIZE_PMC:{
            PMC * const pmc = (PMC *)va_arg(*arg, PMC *);

            return (HUGEFLOATVAL)(VTABLE_get_number(interp, pmc));
        }
    default:
        real_exception(interp, NULL, INVALID_CHARACTER,
                "Internal sprintf doesn't recognize size %d for a float",
                size);
        return (HUGEFLOATVAL)0.0;
    }
}

/*

FUNCDOC: getstring_va

Gets an string out of the C<va_list> in C<obj> and returns it as a
Parrot C<STRING>.

C<size> is an C<enum spf_type_t> value which indicates the storage type
of the string.

*/

static STRING *
getstring_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list * const arg = (va_list *)(obj->data);

    switch (size) {
    case SIZE_REG:
        {
            const char * const cstr = (char *)va_arg(*arg, char *);

            return cstr2pstr(cstr);
        }

    case SIZE_PSTR:
        {
            STRING * const s = (STRING *)va_arg(*arg, STRING *);
            return s ? s : CONST_STRING(interp, "(null)");

        }

    case SIZE_PMC:
        {
            PMC * const pmc = (PMC *)va_arg(*arg, PMC *);
            STRING * const s = VTABLE_get_string(interp, pmc);

            return s;
        }

    default:
        real_exception(interp, NULL, INVALID_CHARACTER,
                "Internal sprintf doesn't recognize size %d for a string",
                size);
        return NULL;
    }
}

/*

FUNCDOC: getptr_va

Gets a C<void *> out of the C<va_list> in C<obj> and returns it.

C<size> is unused.

*/

static void *
getptr_va(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    va_list * const arg = (va_list *)(obj->data);

    return (void *)va_arg(*arg, void *);
}

SPRINTF_OBJ va_core = {
    NULL, 0, getchr_va, getint_va, getuint_va,
    getfloat_va, getstring_va, getptr_va
};

/*

=head2 PMC Functions

FUNCDOC: getchr_pmc

Same as C<getchr_va()> except that a vtable is used to get the value
from C<obj>.

*/

static STRING *
getchr_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    STRING *s;
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    s = VTABLE_get_string(interp, tmp);
    /* XXX string_copy like below? + adjusting bufused */
    return string_substr(interp, s, 0, 1, NULL, 0);
}

/*

FUNCDOC: getint_pmc

Same as C<getint_va()> except that a vtable is used to get the value
from C<obj>.

*/

static HUGEINTVAL
getint_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    HUGEINTVAL ret;
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    ret = VTABLE_get_integer(interp, tmp);

    switch (size) {
    case SIZE_SHORT:
        ret = (short)ret;
        break;
        /* case SIZE_REG: ret=(HUGEINTVAL)(int)ret; break; */
    case SIZE_LONG:
        ret = (long)ret;
        break;
    default:
        /* nothing */ ;
    }

    return ret;
}

/*

FUNCDOC: getuint_pmc

Same as C<getuint_va()> except that a vtable is used to get the value
from C<obj>.

*/

static UHUGEINTVAL
getuint_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    UHUGEINTVAL ret;
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    ret = (UINTVAL)VTABLE_get_integer(interp, tmp);

    switch (size) {
    case SIZE_SHORT:
        ret = (unsigned short)ret;
        break;
        /* case SIZE_REG: * ret=(UHUGEINTVAL)(unsigned int)ret; * break; */
    case SIZE_LONG:
        ret = (unsigned long)ret;
        break;
    default:
        /* nothing */ ;
    }

    return ret;
}

/*

FUNCDOC: getfloat_pmc

Same as C<getfloat_va()> except that a vtable is used to get the value
from C<obj>.

*/

static HUGEFLOATVAL
getfloat_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    HUGEFLOATVAL ret;
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    ret = (HUGEFLOATVAL)(VTABLE_get_number(interp, tmp));

    switch (size) {
    case SIZE_SHORT:
        ret = (HUGEFLOATVAL)(float)ret;
        break;
        /* case SIZE_REG: * ret=(HUGEFLOATVAL)(double)ret; * break; */
    default:
        /* nothing */ ;
    }

    return ret;
}

/*

FUNCDOC: getstring_pmc

Same as C<getstring_va()> except that a vtable is used to get the value
from C<obj>.

*/

static STRING *
getstring_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    STRING *s;
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    s = (STRING *)(VTABLE_get_string(interp, tmp));
    return s;
}

/*

FUNCDOC: getptr_pmc

Same as C<getptr_va()> except that a vtable is used to get the value
from C<obj>.

*/

static void *
getptr_pmc(Interp *interp /*NN*/, INTVAL size, SPRINTF_OBJ *obj /*NN*/)
{
    PMC * const tmp = VTABLE_get_pmc_keyed_int(interp,
            ((PMC *)obj->data),
            (obj->index));

    obj->index++;
    /* XXX correct? */
    return (void *)(VTABLE_get_integer(interp, tmp));
}

SPRINTF_OBJ pmc_core = {
    NULL, 0, getchr_pmc, getint_pmc, getuint_pmc,
    getfloat_pmc, getstring_pmc, getptr_pmc
};

/*

=head1 SEE ALSO

F<src/misc.h>, F<src/misc.c>, F<src/spf_render.c>.

=head1 HISTORY

When I was first working on this implementation of sprintf, I ran into a
problem.  I wanted to re-use the implementation for a Parrot
bytecode-level sprintf, but that couldn't be done, since it used C<va_*>
directly.  For a while I thought about generating two versions of the
source with a Perl script, but that seemed like overkill. Eventually I
came across this idea -- pass in a specialized vtable with methods for
extracting things from the arglist, whatever it happened to be.  This is
the result.

=head1 TODO

In the future, it may be deemed desirable to similarly vtable-ize
appending things to the string, allowing for faster C<PIO_printf()> &c,
as well as a version that writes directly to a C string. However, at
this point neither of those is needed.

*/


/*
 * Local variables:
 *   c-file-style: "parrot"
 * End:
 * vim: expandtab shiftwidth=4:
 */
