#!/usr/bin/perl
use Test::More;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
$context->eval('throw "foobarbaz"');
ok $@,'$@ is set';
like $@,qr/foobarbaz/,'message contained in exception stringification';

is $context->eval('1'),1;
is $@,undef,'$@ is not set';

$context->eval(qq{\n\nthrow "yyy"}, 'test.js');
is $@, "yyy at test.js:3", 'runtime error message contains filename and line number';

$context->eval(qq{\nfunction(\{}, 'test2.js');
like $@, qr{SyntaxError:.* at test2\.js:2}, 'syntax error message contains filename and line number';

eval { $context->eval('(function(f) { throw "js error"; })', 'error.js')->() };
is $@, 'js error at error.js:1';

$context->eval("throw 'привет'");
like $@, qr{привет at.*}, 'unicode errors';

done_testing;

