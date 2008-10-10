#! perl

# Copyright (C) 2006-2008, The Perl Foundation.
# $Id$

use strict;
use warnings;
use lib qw(t . lib ../lib ../../lib ../../../lib);

use Parrot::Test tests => 10;

foreach my $name (qw(Node Val Var Op Block Stmts)) {
    my $module = "'PAST';'$name'";
    my $code   = <<'CODE'
.sub _main :main
    load_bytecode 'PCT.pbc'
    load_bytecode 'library/dumper.pbc'
    .local pmc node
    .local pmc node2
CODE
        ;

    $code .= "    node = new [$module]\n";
    $code .= "    node2 = new [$module]\n";
    $code .= <<'CODE'
    node.'init'('name' => 'foo')
    node2.'init'('name' => 'bar')
    node.'push'(node2)

    $P1 = node.'name'()
    say $P1
    "_dumper"(node, "ast")
    .return ()
.end
CODE
        ;

        $module =~ s/'//g;
    pir_output_is( $code, <<"OUT", "set attributes for $module via method" );
foo
"ast" => PMC '$module'  {
    <name> => "foo"
    [0] => PMC '$module'  {
        <name> => "bar"
    }
}
OUT

}

pir_output_is( <<'CODE', <<'OUT', 'dump PAST::Val node in visual format' );
.sub _main :main
    load_bytecode 'PCT.pbc'
    load_bytecode 'library/dumper.pbc'
    .local pmc node
    node = new ['PAST';'Val']
    node.'value'(1)
    node.'returns'('Integer')
    $P1 = node.'value'()
    say $P1
    $P1 = node.'returns'()
    say $P1
    "_dumper"(node, "ast")
    .return ()
.end
CODE
1
Integer
"ast" => PMC 'PAST;Val'  {
    <value> => 1
    <returns> => "Integer"
}
OUT

## TODO: test that return() is taken from the type of value when not specified

## TODO: check the rest of the PAST::Var attributes
pir_output_is( <<'CODE', <<'OUT', 'dump PAST::Var node in visual format' );
.sub _main :main
    load_bytecode 'PCT.pbc'
    load_bytecode 'library/dumper.pbc'
    .local pmc node
    node = new ['PAST';'Var']
    node.'scope'('foo')
    node.'viviself'('baz')
    node.'lvalue'('buz')
    "_dumper"(node, "ast")
    .return ()
.end
CODE
"ast" => PMC 'PAST;Var'  {
    <scope> => "foo"
    <viviself> => "baz"
    <lvalue> => "buz"
}
OUT

pir_output_is( <<'CODE', <<'OUT', 'dump PAST::Op node in visual format' );
.sub _main :main
    load_bytecode 'PCT.pbc'
    load_bytecode 'library/dumper.pbc'
    .local pmc node
    node = new ['PAST';'Op']
    node.'pasttype'('pirop')
    node.'pirop'('add')
    node.'lvalue'('foo')
    node.'inline'('%r = add %0, %1')
    "_dumper"(node, "ast")
    .return ()
.end
CODE
"ast" => PMC 'PAST;Op'  {
    <pasttype> => "pirop"
    <pirop> => "add"
    <lvalue> => "foo"
    <inline> => "%r = add %0, %1"
}
OUT

pir_output_is( <<'CODE', <<'OUT', 'dump PAST::Block node in visual format' );
.sub _main :main
    load_bytecode 'PCT.pbc'
    load_bytecode 'library/dumper.pbc'
    .local pmc node
    node = new ['PAST';'Block']
    node.'blocktype'('declaration')
    "_dumper"(node, "ast")
    .return ()
.end
CODE
"ast" => PMC 'PAST;Block'  {
    <blocktype> => "declaration"
}
OUT

# Local Variables:
#   mode: cperl
#   cperl-indent-level: 4
#   fill-column: 100
# End:
# vim: expandtab shiftwidth=4:
