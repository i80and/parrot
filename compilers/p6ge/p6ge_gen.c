/*

=head1 NAME

p6ge/p6ge_gen.c - Generate PIR code from a P6 rule expression

=head1 DESCRIPTION

This file contains the functions designed to convert a P6 rule
expression (usually generated by p6ge_parse() ) into the PIR code
that can execute the rule on a string.

=head2 Functions

=over 4

=cut

*/

#include "p6ge.h"
#include "parrot/parrot.h"
#include <stdarg.h>
#include <stdlib.h>

static char* p6ge_cbuf = 0;
static int p6ge_cbuf_size = 0;
static int p6ge_cbuf_len = 0;
static int p6ge_cbuf_lcount = 0;

static void p6ge_gen_exp(P6GE_Exp* e, const char* succ);

/* emit(...) writes strings to an automatically grown string buffer */
static void
emit(const char* fmt, ...)
{
    int lookahead;
    va_list ap;
    lookahead = p6ge_cbuf_len + P6GE_MAX_LITERAL_LEN * 2 + 3 + strlen(fmt) * 2;
    if (lookahead > p6ge_cbuf_size) {
        while (lookahead > p6ge_cbuf_size) p6ge_cbuf_size += 4096;
        p6ge_cbuf = realloc(p6ge_cbuf, p6ge_cbuf_size);
    }
    va_start(ap, fmt);
    p6ge_cbuf_len += vsprintf(p6ge_cbuf + p6ge_cbuf_len, fmt, ap);
    va_end(ap);
}


static void
emitlcount(void)
{
    char* s;
    int lcount = 0;

    for(s = p6ge_cbuf; *s; s++) { if (*s == '\n') lcount++; }
    if (lcount > p6ge_cbuf_lcount + 10) {
        emit("# line %d\n", lcount);
        p6ge_cbuf_lcount = lcount;
    }
}
        

static void
emitsub(const char* sub, ...)
{
    char* s[10];
    int i;
    va_list ap;

    va_start(ap, sub);
    for(i = 0; i < 10; i++) {
        s[i] = va_arg(ap, char*);
        if (!s[i]) break;
        emit("    save %s\n", s[i]);
    }
    va_end(ap);
    emit("    bsr %s\n", sub);
    while (i > 0) emit("    restore %s\n", s[--i]);
}


/* str_con(...) converts string values into PIR string constants */
static char*
str_con(const unsigned char* s, int len)
{
    static char esc[P6GE_MAX_LITERAL_LEN * 2 + 3];
    char* t = esc;
    int i;
    *(t++) = '"';
    for(i = 0; i < len; i++) {
        switch (s[i]) {
            case '\\': *(t++) = '\\'; *(t++) = '\\'; break;
            case '"' : *(t++) = '\\'; *(t++) = '"'; break;
            case '\n': *(t++) = '\\'; *(t++) = 'n'; break;
            case '\r': *(t++) = '\\'; *(t++) = 'r'; break;
            case '\t': *(t++) = '\\'; *(t++) = 't'; break;
            case '\0': *(t++) = '\\'; *(t++) = '0'; break;
            default  : *(t++) = s[i]; break;
        }
    }
    *(t++) = '"';
    *t = 0;
    return esc;
}


static void
trace(const char* fmt, ...)
{
    char s[128];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(s, fmt, ap);
    va_end(ap);
    emit("    print %s\n", str_con(s, strlen(s)));
    emit("    print \" at \"\n");
    emit("    print pos\n");
    emit("    print \"\\n\"\n");
}

 
static void
p6ge_gen_pattern_end(P6GE_Exp* e, const char* succ)
{
    emit("R%d:                               # end of pattern\n", e->id);
    emit("    .yield(pos)\n");
    emit("    goto fail\n");
}


static void
p6ge_gen_dot(P6GE_Exp* e, const char* succ)
{
    emit("R%d:                               # dot {%d..%d}%c\n", 
         e->id, e->min, e->max, (e->isgreedy) ? ' ' : '?');
    emit("    maxrep = length target\n");
    emit("    maxrep -= pos\n");
    if (e->max != P6GE_INF) {
        emit("    if maxrep <= %d goto R%d_1\n", e->max, e->id);
        emit("    maxrep = %d\n", e->max);
        emit("  R%d_1:\n", e->id);
    }
    if (e->isgreedy) {
        emit("    rep = maxrep\n");
        emit("    pos += rep\n");
        emit("  R%d_2:\n", e->id);
        emit("    if rep == %d goto %s\n", e->min, succ);
        emit("    if rep < %d goto fail\n", e->min);
        emitsub(succ, "pos", "rep", 0);
        emit("    dec rep\n");
        emit("    dec pos\n");
        emit("    goto R%d_2\n\n", e->id);
    }
    else { /* dot lazy */
        emit("    rep = %d\n", e->min);
        if (e->min > 0) emit("    pos += %d\n", e->min);
        emit("  R%d_3:\n", e->id);
        emit("    if rep == maxrep goto %s\n", succ);
        emit("    if rep > maxrep goto fail\n");
        emitsub(succ, "pos", "rep", "maxrep", 0);
        emit("    inc rep\n");
        emit("    inc pos\n");
        emit("    goto R%d_3\n\n", e->id);
    }
}


static void
p6ge_gen_literal(P6GE_Exp* e, const char* succ)
{
    emit("R%d:                               # %.16s {%d..%d}%c\n", 
         e->id, str_con(e->name, e->nlen), e->min, e->max, 
         (e->isgreedy) ? ' ' : '?');

    if (e->min==1 && e->max==1) {
        emit("    substr $S0, target, pos, %d\n", e->nlen);
        emit("    if $S0 != %s goto fail\n", str_con(e->name, e->nlen));
        emit("    pos += %d\n", e->nlen);
        emit("    goto %s\n\n", succ);
        return;
    }

    if (e->isgreedy) {
        emit("    rep = 0\n");
        emit("  R%d_1:\n", e->id);
        if (e->max != P6GE_INF)
            emit("    if rep >= %d goto R%d_2\n", e->max, e->id);
        emit("    substr $S0, target, pos, %d\n", e->nlen);
        emit("    if $S0 != %s goto R%d_2\n", str_con(e->name, e->nlen), e->id);
        emit("    inc rep\n");
        emit("    pos += %d\n", e->nlen);
        emit("    goto R%d_1\n", e->id);
        emit("  R%d_2:\n", e->id);
        emit("    if rep == %d goto %s\n", e->min, succ);
        if (e->min > 0)
            emit("    if rep < %d goto fail\n", e->min);
        emitsub(succ, "pos", "rep", 0);
        emit("    dec rep\n");
        emit("    pos -= %d\n", e->nlen);
        emit("    goto R%d_2\n\n", e->id);
        return;
    } 
    else { /* islazy */
        emit("    rep = 0\n");
        emit("  R%d_1:\n", e->id);
        if (e->max != P6GE_INF) 
            emit("    if rep == %d goto %s\n", e->max, succ);
        if (e->min > 0)
            emit("    if rep < %d goto R%d_2:\n", e->min, e->id);
        emitsub(succ, "pos", "rep", 0);
        emit("  R%d_2:\n", e->id);
        emit("    substr $S0, target, pos, %d\n", e->nlen);
        emit("    if $S0 != %s goto fail\n", str_con(e->name, e->nlen));
        emit("    inc rep\n");
        emit("    pos += %d\n", e->nlen);
        emit("    goto R%d_1\n\n", e->id);
        return;
    } 
}


static void
p6ge_gen_concat(P6GE_Exp* e, const char* succ)
{
    char succ2[20];
    
    emit("R%d:                               # concat R%d, R%d\n", 
         e->id, e->exp1->id, e->exp2->id);
    sprintf(succ2,"R%d",e->exp2->id);
    p6ge_gen_exp(e->exp1, succ2);
    p6ge_gen_exp(e->exp2, succ);
}


/* XXX: add some docs that describes how this works! */
/* XXX: add check to prevent infinite recursion on zero-length match */
static void
p6ge_gen_group(P6GE_Exp* e, const char* succ)
{
    char repsub[32];
    char r1sub[32];
    char key[32];
    char c1, c2;

    c1 = '['; c2 = ']';
    if (e->group >= 0) { c1 = '('; c2 = ')'; }
    sprintf(repsub, "R%d_repeat", e->id);
    sprintf(r1sub, "R%d", e->exp1->id);
    sprintf(key,"\"%d\"", e->group);

    emit("R%d:                               # group %s %c R%d %c {%d..%d}%c\n",
         e->id, key, c1, e->exp1->id, c2, 
         e->min, e->max, (e->isgreedy) ? ' ' : '?');
    /* for unquantified, non-capturing groups, don't bother with the
       group code */
    if (e->min==1 && e->max==1 && e->group<0) {
        p6ge_gen_exp(e->exp1, succ);
        return;
    }

    /* otherwise, we have work to do */

    /* GROUP: initialization
       This first part sets up the initial structures for a repeating group. 
       We need a repeat count and (possibly) a captures hash. */
    emit("    classoffset $I0, match, \"P6GEMatch\"\n");
    emit("    $I0 += 3\n");
    emit("    getattribute gr_rep, match, $I0\n");
    emit("    $I1 = exists gr_rep[%s]\n", key);
    emit("    if $I1 goto R%d_1\n", e->id);
    emit("    new $P1, .PerlInt\n");
    emit("    gr_rep[%s] = $P1\n", key);
    emit("  R%d_1:\n", e->id);

    if (e->group >= 0) { 
        emit("    inc $I0\n");
        emit("    getattribute gr_cap, match, $I0\n");
        emit("    $I1 = exists gr_cap[%s]\n", key);
        emit("    if $I1 goto R%d_2\n", e->id);
        emit("    new $P1, .PerlArray\n");
        emit("    gr_cap[%s] = $P1\n", key);
        emit("  R%d_2:\n", e->id);
    }

    emit("    $P1 = gr_rep[%s]\n", key);
    emitsub(repsub, "pos", "gr_rep", "$P1", 0);
    emit("    gr_rep[%s] = $P1\n", key);
    emit("    goto fail\n\n");

    /* GROUP: repeat code
       This code is called whenever we reach the end of the group's
       subexpression.  It handles closing any outstanding capture, and 
       repeats the group if the quantifier requires it. */
    emit("%s:\n", repsub);
    emit("    classoffset $I0, match, \"P6GEMatch\"\n");
    emit("    $I0 += 3\n");
    emit("    getattribute $P0, match, $I0\n");
    emit("    gr_rep = $P0[%s]\n", key);
    if (e->group >= 0) { 
        emit("    inc $I0\n");
        emit("    getattribute $P0, match, $I0\n");
        emit("    gr_cap = $P0[%s]\n", key);
        emit("    if gr_rep < 1 goto %s_1\n", repsub);  /* save prev cap end */
        emit("    push gr_cap, pos\n");
    }

    emit("  %s_1:\n", repsub);
    if (e->isgreedy) {
        if (e->max != P6GE_INF) 
            emit("    if gr_rep >= %d goto %s_2\n", e->max, repsub);
        emit("    inc gr_rep\n");
        if (e->group >= 0)
            emit("    push gr_cap, pos\n");         /* save next cap start */
        emitsub(r1sub, "pos", "gr_cap", "gr_rep", 0);
        if (e->group >= 0)
            emit("    $I0 = pop gr_cap\n");        /* remove next cap start */
        emit("    dec gr_rep\n");
        emit("  %s_2:\n", repsub);
        if (e->min > 0) 
            emit("    if gr_rep < %d goto %s_fail\n", e->min, repsub);
        emitsub(succ, "pos", "gr_cap", "gr_rep", 0);
    } 
    else { /* group lazy */
        if (e->min > 0)
            emit("    if gr_rep < %d goto %s_3\n", e->min, repsub);
        emitsub(succ, "pos", "gr_cap", "gr_rep", 0);
        emit("  %s_3:\n", repsub);
        if (e->max != P6GE_INF)
            emit("    if gr_rep >= %d goto %s_fail\n", e->max, repsub);
        emit("    inc gr_rep\n");
        if (e->group >= 0) 
            emit("    push gr_cap, pos\n");         /* save next cap start */
        emitsub(r1sub, "pos", "gr_cap", "gr_rep", 0);
        if (e->group >= 0)
            emit("    $I0 = pop gr_cap\n");        /* remove next cap start */
        emit("    dec gr_rep\n");
    }  /* group lazy */

    emit("  %s_fail:\n", repsub);
    if (e->group >= 0) {
        emit("    if gr_rep < 1 goto fail\n", repsub);  
        emit("    $I0 = pop gr_cap\n");             /* remove prev cap end */
    }
    emit("    goto fail\n\n");

    p6ge_gen_exp(e->exp1, repsub);
}


static void
p6ge_gen_alt(P6GE_Exp* e, const char* succ)
{
    char r1sub[32];

    sprintf(r1sub,"R%d", e->exp1->id);
    emit("R%d:                               # alt R%d | R%d\n", 
         e->id, e->exp1->id, e->exp2->id);
    emitsub(r1sub, "pos", 0);
    emit("    goto R%d\n\n", e->exp2->id);

    p6ge_gen_exp(e->exp1, succ);
    p6ge_gen_exp(e->exp2, succ);
}
    

static void
p6ge_gen_anchor(P6GE_Exp* e, const char* succ)
{

    switch(e->type) {
    case P6GE_ANCHOR_BOS:
        emit("R%d:                               # ^anchor\n", e->id);
        emit("    if pos != 0 goto fail\n");
        emit("    goto %s\n", succ);
        return;
    case P6GE_ANCHOR_EOS:
        emit("R%d:                               # anchor$\n", e->id);
        emit("    if pos != lastpos goto fail\n");
        emit("    goto %s\n", succ);
        return;
    case P6GE_ANCHOR_BOL:
        emit("R%d:                               # ^^anchor\n", e->id);
        emit("    if pos == 0 goto %s\n", succ);
        emit("    if pos == lastpos goto fail\n");
        emit("    $I0 = pos - 1\n");
        emit("    substr $S0, target, $I0, 1\n");
        emit("    if $S0 == \"\\n\" goto %s\n", succ);
        emit("    goto fail\n\n");
        return;
    case P6GE_ANCHOR_EOL:
        emit("R%d:                               # anchor$$\n", e->id);
        emit("    if pos == lastpos goto R%d_1\n", e->id);
        emit("    substr $S0, target, pos, 1\n");
        emit("    if $S0 == \"\\n\" goto %s\n", succ);
        emit("    goto fail\n");
        emit("R%d_1:\n", e->id);
        emit("    $I0 = pos - 1\n");
        emit("    substr $S0, target, $I0, 1\n");
        emit("    if $S0 != \"\\n\" goto %s\n", succ);
        emit("    goto fail\n\n");
        return;
    default: break;
    }
}


static void 
p6ge_gen_exp(P6GE_Exp* e, const char* succ)
{
    emitlcount();
    switch (e->type) {
    case P6GE_NULL_PATTERN: emit("R%d: goto %s\n", e->id, succ); break;
    case P6GE_PATTERN_END: p6ge_gen_pattern_end(e, succ); break;
    case P6GE_DOT: p6ge_gen_dot(e, succ); break;
    case P6GE_LITERAL: p6ge_gen_literal(e, succ); break;
    case P6GE_CONCAT: p6ge_gen_concat(e, succ); break;
    case P6GE_GROUP: p6ge_gen_group(e, succ); break;
    case P6GE_ALT: p6ge_gen_alt(e, succ); break;
    case P6GE_ANCHOR_BOS:
    case P6GE_ANCHOR_EOS: 
    case P6GE_ANCHOR_BOL:
    case P6GE_ANCHOR_EOL: p6ge_gen_anchor(e, succ); break;
    }
}


static char* 
p6ge_gen(P6GE_Exp* e)
{
    char r1sub[32];
    p6ge_cbuf_len = 0;
    p6ge_cbuf_lcount = 0;

    emit(".sub _P6GE_Rule\n");
    emit("    .param string target\n");
    emit("    .local pmc match\n");
    emit("    .local pmc rulecor\n");
    emit("  class_loaded:\n");
    emit("    find_type $I0, \"P6GEMatch\"\n");
    emit("    new match, $I0\n");
    emit("    newsub rulecor, .Coroutine, _Rule_cor\n");
    emit("    match.\"_init\"(target, rulecor)\n");
    emit("    match.\"_next\"()\n");
    emit("    .return(match)\n");
    emit(".end\n\n");

    emit(".sub _Rule_cor\n");
    emit("    .param pmc match\n");
    emit("    .param string target\n");
    emit("    .param int pos\n");
    emit("    .param int lastpos\n");
    emit("    .local int rep\n");
    emit("    .local int maxrep\n");
    emit("    .local pmc gr_rep\n");
    emit("    .local pmc gr_cap\n");
    sprintf(r1sub, "R%d", e->id);
    emitsub(r1sub, 0);
    emit("  fail_forever:\n");
    emit("    .yield(-1)\n");
    emit("    goto fail_forever\n\n");

    p6ge_gen_exp(e, 0);
    emit("  fail:\n    pos = -1\n    ret\n");
    emit(".end\n");

    return p6ge_cbuf;
}

/* is_bos_anchored() returns true if an expression is anchored to the bos */
static int 
is_bos_anchored(P6GE_Exp* e)
{
    switch (e->type) {
    case P6GE_ANCHOR_BOS: return 1;
    case P6GE_CONCAT: 
        return is_bos_anchored(e->exp1) || is_bos_anchored(e->exp2);
    case P6GE_GROUP: return is_bos_anchored(e->exp1);
    case P6GE_ALT: return is_bos_anchored(e->exp1) && is_bos_anchored(e->exp2);
    default: break;
    }
    return 0;
}

/*

=item C<char* p6ge_p6rule_pir(const unsigned char* s)>

Converts the rule expression in s to its equivalent PIR code.
This function calls p6ge_parse() to build an expression tree from
the string in s, then calls p6ge_gen() to generate a PIR subroutine
from the expression tree.

=cut

*/

char*
p6ge_p6rule_pir(const unsigned char* s)
{
    P6GE_Exp* e = 0;
    P6GE_Exp* dot0 = 0;
    char* pir = 0;

    e = p6ge_parse_new(P6GE_CONCAT,
                           p6ge_parse_new(P6GE_GROUP, p6ge_parse(s), 0),
                           p6ge_parse_new(P6GE_PATTERN_END, 0, 0));

    if (!is_bos_anchored(e)) {
        dot0 = p6ge_parse_new(P6GE_DOT, 0, 0);
        dot0->min = 0; dot0->max = P6GE_INF; dot0->isgreedy = 0;
        e = p6ge_parse_new(P6GE_CONCAT, dot0, e);
    }

    pir = p6ge_gen(e);
    p6ge_parse_free(e);
    return pir;
}


/*

=item C<char* p6ge_p5rule_pir(const char* s)>

Converts the P5 rule expression in s to its equivalent PIR code.
This function calls p5re_parse() to build an expression tree from
the string in s, then calls p6ge_gen() to generate a PIR subroutine
from the expression tree.

=cut

*/

char*
p6ge_p5rule_pir(const unsigned char* s)
{
    P6GE_Exp* e = 0;
    P6GE_Exp* dot0 = 0;
    char* pir = 0;

    e = p6ge_parse_new(P6GE_CONCAT,
                           p6ge_parse_new(P6GE_GROUP, p6ge_parse(s), 0),
                           p6ge_parse_new(P6GE_PATTERN_END, 0, 0));

    if (!is_bos_anchored(e)) {
        dot0 = p6ge_parse_new(P6GE_DOT, 0, 0);
        dot0->min = 0; dot0->max = P6GE_INF; dot0->isgreedy = 0;
        e = p6ge_parse_new(P6GE_CONCAT, dot0, e);
    }

    pir = p6ge_gen(e);
    p6ge_parse_free(e);
    return pir;
}


/*

=item C<void Parrot_lib_p6ge_init(Parrot_Interp interpreter, PMC* lib)>

Used when this module is loaded dynamically by Parrot's loadlib
instruction -- automatically initializes the p6ge engine.

=cut

*/

void 
Parrot_lib_p6ge_init(Parrot_Interp interpreter, PMC* lib)
{
    p6ge_init();
}

/*

=back

=head1 SEE ALSO

F<p6ge/p6ge.h> and F<p6ge/p6ge_parse.c>

=head1 HISTORY

Initial version by Patrick R. Michaud on 2004.11.16

=cut

*/

/*
 * Local variables:
 * c-indentation-style: bsd
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
