#!/usr/bin/perl

use utf8;
use strict;
use warnings;

use Test::More;

use FindBin;
my $context = require "$FindBin::Bin/mem.pl";

package Test;

sub new {
    my ($class, $val) = @_;
    bless { val => $val }, $class
}

package main;

for (1..100000) {
    print STDERR "$_\r";
    $context->eval('(function(data) { var x = data; })')->(Test->new($_));
}

1 while !$context->idle_notification;

SKIP: {
    skip "no ps", 1 unless check_ps();
    ok get_rss() < 50_000, 'objects are released';
}

done_testing;
