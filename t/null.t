#!/usr/bin/perl

use strict;
use warnings;

use JavaScript::V8;
use Test::More;

my $c = JavaScript::V8::Context->new;

my $value = $c->eval("[{'x': null}]");
eval { $value->[0]{x} = 1 };
is $@, '';

done_testing;
