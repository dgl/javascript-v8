#!/usr/bin/perl
use Test::More;
use Test::Number::Delta within => 1e-9;
use JavaScript::V8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
ok $context,"creating new JavaScript::V8::Context";
my $val = $context->eval("777");
is $val,777,"integers";

{
  use utf8;
  is($context->eval("'Μπορώ να φάω σπασμένα γυαλιά χωρίς να πάθω τίποτα'"), "Μπορώ να φάω σπασμένα γυαλιά χωρίς να πάθω τίποτα", 'unicode strings');
}

is($context->eval("'\x{a3}'"), "\x{a3}", 'latin-1 strings');

delta_ok($context->eval("1.34"), 1.34, 'numbers');
ok(!defined $context->eval('undefined'), 'undefined');
ok(!defined $context->eval('null'), 'null');

$context->name_global('window');
$context->eval(q{x = 42});
is $context->eval(q{window.x}), 42, "Window is the same as top level";

done_testing;
