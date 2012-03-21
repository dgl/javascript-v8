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

$context->eval('var DATA = []');
for (1..100000) {
    print STDERR "$_\r";
    $context->eval('(function(data) { DATA.push(data); })')->(Test->new($_));
}

1 while !$context->idle_notification;

SKIP: {
    skip "no ps", 1 unless check_ps();
    ok get_rss() > 100_000, 'objects are not released';
}

done_testing;
