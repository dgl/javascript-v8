use strict;
use warnings;
use Test::More;
use JavaScript::V8;
use JSON;

my $v8context = JavaScript::V8::Context->new();

$v8context->bind( f => JSON::false );
is $v8context->eval('(function() { return (f ? 1 : 0) })()'), 0, 'Testing false - should return 0';

$v8context->bind( f => JSON::true );
is $v8context->eval('(function() { return (f ? 1 : 0) })()'), 1, 'Testing true - should return 1';

is $v8context->eval('typeof f'), 'boolean', 'Testing the Javascript type is a boolean';

done_testing;
