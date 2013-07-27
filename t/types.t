#!/usr/bin/perl

use Test::More;
use Test::Number::Delta within => 1e-9;
use JavaScript::V8;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new;

is $context->eval('(function(v) { return v })')->(1000000000000), 1000000000000, 'big numbers are ok';
is $context->eval('(function(v) { return v })')->(2174652970), 2174652970, '32-bit numbers  are ok';
is $context->eval('(function(v) { return v })')->(-2174652970), -2174652970, '32-bit numbers  are ok';
is $context->eval('(function(v) { return v })')->(-2170), -2170, '32-bit numbers  are ok';
is $context->eval('(function(v) { return v })')->(2170), 2170, '32-bit numbers  are ok';
delta_ok $context->eval('(function(v) { return v })')->(2.34234), 2.34234, 'real numbers are ok';

is $context->eval('(function(v) { return v + 2; })')->(2), 4;
is $context->eval('(function(v) { return v + 2; })')->('2'), '22', 'string converts into a string';

my $val = '3'; 
if ($val > 3) {

}
is $context->eval('(function(v) { return v + 2; })')->($val), '32', 'string conversion after numeric comparison';

is $context->eval('"тест"'), 'тест', 'utf8 ok';
is $context->eval('(function(v) { return v; })')->('тест'), 'тест';

done_testing;
