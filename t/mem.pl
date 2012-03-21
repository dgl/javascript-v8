use JavaScript::V8;
use Data::Dumper;

use utf8;
use strict;
use warnings;

my $context = JavaScript::V8::Context->new;

$context->bind( warn => sub { warn(@_) });
$context->set_flags_from_string('--expose-gc');

sub check_ps {
    system('ps >/dev/null 2>&1') == 0
}

sub get_rss {
    +{ map { split ' ' } `ps -o pid,rss` }->{$$}
}

$context
