#!/usr/bin/env perl 

use strict;
use warnings;
use utf8;

use Test::More;

use JavaScript::V8;
use JSON::XS;
use Types::Serialiser;

my $v8context = JavaScript::V8::Context->new();
$v8context->bind( f => Types::Serialiser::false );
ok($v8context->eval('(function() { return (f ? 1 : 0) })()') == 0, 'Testing false - should return 0');
$v8context->bind( f => Types::Serialiser::true );
ok($v8context->eval('(function() { return (f ? 1 : 0) })()') == 1, 'Testing true - should return 1');
ok($v8context->eval('typeof f') eq 'boolean', 'Testing the Javascript type is a boolean');

done_testing;
