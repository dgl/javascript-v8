package JavaScript::V8;
use strict;
use warnings;

our $VERSION = '0.05';

use JavaScript::V8::Context;
require XSLoader;
XSLoader::load('JavaScript::V8', $VERSION);

1;
__END__

=encoding utf8

=head1 NAME

JavaScript::V8 - Perl interface to the V8 JavaScript engine

=head1 SYNOPSIS

  use JavaScript::V8;

  my $context = JavaScript::V8::Context->new();

  $context->bind_function(write => sub { print @_ });

  $context->eval(q/
    for (i = 99; i > 0; i--) {
        write(i + " bottle(s) of beer on the wall, " + i + " bottle(s) of beer\n");
        write("Take 1 down, pass it around, ");
        if (i > 1) {
            write((i - 1) + " bottle(s) of beer on the wall.");
        }
        else {
            write("No more bottles of beer on the wall!");
        }
    }
  /);

=head1 INSTALLING V8

=head2 From Source

See L<V8 issue 413|http://code.google.com/p/v8/issues/detail?id=413> if you are
having trouble on gcc 4.4.1.

    svn checkout http://v8.googlecode.com/svn/trunk/ v8
    cd v8
    scons library=shared
    sudo mv include/v8.h /usr/local/include/
    sudo mv libv8.so /usr/local/lib/

If you're using a 64-bit architecture, you need to include C<arch=x64> when
running scons:

    scons library=shared arch=x64

Otherwise, perl will complain when trying to link/load v8. For more
information, see L<V8 issue
330|http://code.google.com/p/v8/issues/detail?id=330>.

=head3 On OS X

On OS X I've successfully used L<Homebrew|http://mxcl.github.com/homebrew/>,
install Homebrew then:

  brew install v8

=head2 Binary

=head3 Linux

On Ubuntu 10.04 (and possibly Debian), the library and header files can be installed by running:

    sudo aptitude install libv8-2.0.3 libv8-dev

Similar packages may be available for other distributions (adjust the package names accordingly).

=head1 REPOSITORY

The source code lives at L<http://github.com/dgl/javascript-v8>.

=head1 SEE ALSO

=over

=item * L<JavaScript>

=item * L<JavaScript::Lite>

=item * L<JavaScript::SpiderMonkey>

=item * L<JavaScript::V8x::TestMoreish>

=item * L<JE>

=back

=head1 AUTHORS

  Pawel Murias <pawelmurias at gmail dot com>
  David Leadbeater <dgl@dgl.cx>

=head1 COPYRIGHT AND LICENSE

  Copyright (c) 2009-2010 Paweł Murias
  Copyright (c) 2011 David Leadbeater

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

=head1 ACKNOWLEDGMENTS

=over

=item Claes Jakobsson <claesjac at cpan dot org>

I stole and adapted pieces of docs and API design from JavaScript.pm

=item Brian Hammond <brain @ fictorial dot com>

For salvaging the code of V8.pm from a message board (which I took some code
and the idea from)

=item The hacker who wrote V8.pm and posted it on the message board

(L<http://d.hatena.ne.jp/dayflower/20080905/1220592409>)

=item All the fine people at #perl@freenode.org for helping me write this module

=back

=cut
