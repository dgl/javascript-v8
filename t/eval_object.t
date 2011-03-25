#!/usr/bin/perl
use Test::More tests => 11;
use JavaScript::V8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new();

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
    my %expected = ( "\x{1234}" => { bar => { boo => 'far' } } );
    is_deeply($context->eval('x={"\u1234":{bar:{boo:"far"}}}'), \%expected);
    is_deeply($context->eval('x'), \%expected);
};

{
    my %expected = ( "\x{a3}" => { bar => { boo => 'far' } } );
    is_deeply($context->eval('x={"\u00a3":{bar:{boo:"far"}}}'), \%expected);die $@ if $@;
};

my $code = $context->eval('function x() { return 2+2 }; x');
isa_ok $code, "CODE";
is $code->(), 4;

done_testing;

