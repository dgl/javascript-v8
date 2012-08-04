#!/usr/bin/perl
use Test::More tests => 4;
use JavaScript::V8;
use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();
$context->eval('2/;');
ok $@, '$@ is set';
like $@, qr/SyntaxError: Unexpected token/, 'Error returned properly';
like $@, qr/at eval:1/, 'Default code source/origin properly cased';

$context->eval('2/;', 'fake.js');
like $@, qr/at fake\.js/, 'Code source/origin reported in errors';
