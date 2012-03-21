#!/usr/bin/perl

use utf8;
use strict;
use warnings;

use Test::More;

use FindBin;
my $context = require "$FindBin::Bin/mem.pl";

for (1..200000) {
    print STDERR "$_\r";
    $context->eval('(function(data) { var x = data; })')->(sub { 1 });
}

1 while !$context->idle_notification;

SKIP: {
    skip "no ps", 1 unless check_ps();
    ok get_rss() < 50_000, 'functions are released';
}

done_testing;
