use strict;
use warnings;
use Test::More;
use JavaScript::V8;

run_test("JSON::${_}::Boolean") for qw(XS PP);

done_testing;

sub run_test {
  my ($class) = @_;
  my $v8context = JavaScript::V8::Context->new;

  $v8context->bind( f => bless( do{\(my $o = 0)}, $class ) );
  is $v8context->eval('(function() { return (f ? 1 : 0) })()'), 0, 'Testing false - should return 0';

  $v8context->bind( f => bless( do{\(my $o = 1)}, $class ) );
  is $v8context->eval('(function() { return (f ? 1 : 0) })()'), 1, 'Testing true - should return 1';

  is $v8context->eval('typeof f'), 'boolean', 'Testing the Javascript type is a boolean';
}
