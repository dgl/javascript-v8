#!/usr/bin/perl
use Test::More;
use JavaScript::V8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

my $called_foo = 0;
$context->bind_function(foo => sub {
   $called_foo = 1; 
   'arg';
});

my $arg;
$context->bind_function(bar => sub {
    $arg = $_[0];
});

my $retval = $context->eval("foo()");
ok($called_foo,'calling p5 function from js');
is($retval, 'arg', 'return value');
$context->eval("bar(foo())");
is $arg,'arg','passing arguments';

my $expected = {a=>[1,"2",3], b=>"coucou"};
$context->bind_function(foo2 => sub {
    return $expected;
});
is_deeply($context->eval("foo2()"), $expected, 'deep');

# ---- Roundtripping
$context->bind(x => sub { 2*shift });
is $context->eval("x")->(3), 6;

$context->bind(y => $context->eval("x"));
is $context->eval("y")->(3), 6, "Roundtrip";

$context->bind(z => { z => $context->eval("y") });
is $context->eval("z")->{z}->(3), 6, "Roundtrip via object";

is $context->eval('(function(x) { return x; })')->(undef), undef, 'undef roundtrip';

my $zzz = 1;
undef $zzz;
is $context->eval('(function(x) { return x; })')->($zzz), undef, 'undef roundtrip 2';

is $context->eval('(function(f) { try { f() } catch(e) { return "ok"; } })')->(sub { die 'err' }), 'ok', 'caught perl error in js';

done_testing;
