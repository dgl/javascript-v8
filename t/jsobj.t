#!/usr/bin/perl

use Test::More;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new( bless_prefix => 'JS::' );

$context->bind( warn => sub { warn(@_) });

my $COUNTER_SRC = <<'END';
function Counter() {
    this.val = 1;
}

Counter.prototype.inc = function() {
    this.val++;
}

Counter.prototype.get = function() {
    return this.val;
}

Counter.prototype.set = function(newVal) {
    this.val = newVal;
}

Counter.prototype.copyFrom = function(otherCounter) {
    this.set(otherCounter.get());
}

Counter.prototype.__perlPackage = "Counter";

new Counter;
END

my $c1 = $context->eval($COUNTER_SRC);

isa_ok $c1, 'JS::Counter';
is $c1->get, 1, 'initial value';

$c1->inc;
$c1->inc;

is $c1->get, 3, 'method calls work';

$c1->set('8');

is $c1->get, 8, 'method with an argument';

{
    my $c2 = $context->eval('new Counter');

    $c2->copyFrom($c1);
    $c2->inc;

    is $c2->get, 9, 'converting perl object wrapper back to js';
}

{
    my $context = JavaScript::V8::Context->new( enable_blessing => 1 );
    
    $context->eval($COUNTER_SRC);
    my $c = $context->eval('new Counter()');

    isa_ok $c, 'Counter', 'enable_blessing option works';
}

{
    my $context = JavaScript::V8::Context->new;
    
    $context->eval($COUNTER_SRC);
    my $c = $context->eval('var c = new Counter(); c.set(77); c');

    ok !eval { $c->isa('Counter') }, 'no blessing without enable_blessing or bless_prefix options';
    is $c->{val}, 77, 'no blessing, object converted as data';
}

done_testing;
