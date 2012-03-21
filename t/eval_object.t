#!/usr/bin/perl
use Test::More tests => 14 + 2*1000;
use JavaScript::V8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new( enable_wantarray => 1 );

{
    my %expected = ( foo => 'bar' );
    is_deeply($context->eval('(function(){ return {foo: "bar"}})();'), \%expected);
};

{ 
    my %expected = ( foo => 'bar', boo => 123 );
    is_deeply($context->eval('(function(){ return {boo:123,foo: "bar"}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => 'blatt' }, boo => 123 );
    is_deeply($context->eval('(function(){ return {foo:{bar:"blatt"},boo:123}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => 'blatt', boo => [123,456] } );
    is_deeply($context->eval('(function(){ return {foo:{bar:"blatt",boo:[123,456]}}})();'), \%expected);
};

{
    my %expected = ( foo => [ {} ] );
    is_deeply($context->eval('(function(){ return {foo:[{}]}})();'), \%expected);
};

{
    my %expected = ( foo => { bar => { boo => 'far' } } );
    is_deeply($context->eval('(function(){ return {foo:{bar:{boo:"far"}}}})();'), \%expected);
};

{
    my %expected = ( "\x{1234}" => { bar => { boo => "far\x{0}" } } );
    is_deeply($context->eval(qq[x={"\x{1234}":{bar:{boo:"far\x{0}"}}}]), \%expected);
    is_deeply($context->eval('x={"\u1234":{bar:{boo:"far\u0000"}}}'), \%expected);
    is_deeply($context->eval('x'), \%expected);
};

{
    my %expected = ( "\x{a3}" => { bar => { boo => "far\x{a3}" } } );
    is_deeply($context->eval('x={"\u00a3":{bar:{boo:"far\u00a3"}}}'), \%expected);die $@ if $@;
};

is_deeply [ $context->eval('var f = (function() { return [1,2,3]; }); f.__perlReturnsList = true; f')->() ], [1,2,3], 'array returns as a list in list context';
#is_deeply [ $context->eval('[1,2,3,4,5]') ], [1,2,3,4,5], 'eval in list context'; 

for(1 .. 1000) {
  my $code = $context->eval('function x() { return 2+2 }; x');
  isa_ok $code, "CODE";
  is $code->(), 4, 'returns expected value';
}

my $errcv = $context->eval('function err() { throw new Error("fail") }; err');
eval { $errcv->() };
like $@, qr/fail/, 'got proper exception';

my $f1 = $context->eval('var f = function(a) { return a; }; f');
my $f2 = $context->eval('f');
my $f3 = $f2->($f1)->($f2);

is $f1, $f2, 'roundtrip - same perl object';
is $f1, $f3, 'roundtrip - same perl object';

done_testing;
