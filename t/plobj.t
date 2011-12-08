#!/usr/bin/perl

use Test::More;
use Data::Dumper;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

$context->bind( 
    warn => sub { warn(@_) },
);

$context->bind(
    dump => sub { warn Dumper $_[0] },
);

package Counter;

sub new {
    my $class = shift;
    bless { val => 1 }, $class;
}

sub inc {
    my $self = shift;
    $self->{val}++
}

sub get {
    my $self = shift;
    $self->{val}
}

sub set {
    my ($self, $new) = @_;
    $self->{val} = $new;
}

sub copy_from {
    my ($self, $other_counter) = @_;
    $self->set($other_counter->get);
}

sub DESTROY {
    my $self = shift;
    ($self->{on_destroy} || sub {})->();
}
    
package main;

$context->eval(<<'END');
function test1(counter1) {
    return counter1.get() == 1;
}
function test2(counter1, counter2) {
    counter1.copy_from(counter2);
    return counter1.get();
}
END

my $c1 = Counter->new;
my $c2 = Counter->new;

ok $context->eval('test1')->($c1);

$c1->set(8);
$c2->set(2);
is $context->eval('test2')->($c1, $c2), 2;

is $context->eval('(function(c) { return c })')->($c1), $c1;

{
    my $destroyed;
    {
        my $c = Counter->new;
        $c->set(42);
        $c->{on_destroy} = sub { $destroyed = 1 };
        $context->eval('(function(c) { C = c })')->($c);
    }

    ok !$destroyed;
    is $context->eval('C')->get, 42;
}

{
    my $destroyed;
    {
        my $c = Counter->new;
        $c->set(42);
        $c->{on_destroy} = sub { $destroyed = 1 };
        $context->eval('(function(c) { return c.get() + 1; })')->($c);
    }

    ok $destroyed;
}

done_testing;
