#!/usr/bin/perl

use Test::More;
use Data::Dumper;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

my $x = {};
$x->{y} = $x;

is_deeply $context->eval('(function(v) { return v; })')->($x), $x, 'circular object roundtrip';

my $y = [];
$y->[0] = $y;
is_deeply $context->eval('(function(v) { return v; })')->($y), $y, 'circular array roundtrip';

done_testing;
