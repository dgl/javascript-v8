# NAME

JavaScript::V8 - Perl interface to the V8 JavaScript engine

# SYNOPSIS

    use JavaScript::V8;

    my $context = JavaScript::V8::Context->new();

    $context->bind( write => sub { print @_ } );
    $context->bind( bottles => 3 );
    $context->bind( wine_type => ['red', 'white', 'sparkling'] );

    $context->bind( wine_type_description => {
        white     => "White wine is a wine whose color is slightly yellow. This kind of wine is produced using non-coloured grapes or using red-skinned grapes' juice, not allowing it to extract pigment from the skin.",
        red       => "Red wine is a type of wine made from dark-coloured (black) grape varieties. The actual colour of the wine can range from intense violet, typical of young wines, through to brick red for mature wines and brown for older red wines.",
        sparkling => "Sparkling wine is a wine with significant levels of carbon dioxide in it making it fizzy. The carbon dioxide may result from natural fermentation, either in a bottle, as with the méthode champenoise, in a large tank designed to withstand the pressures involved (as in the Charmat process), or as a result of carbon dioxide injection.",
    });

    $context->eval(q/
        for (i = bottles; i > 0; i--) {
            var type = wine_type[i - 1];
            var description = wine_type_description[type];

            write(i + " bottle(s) of wine on the wall, " + i + " bottle(s) of wine\n");

            write("This is bottle of " + type + " wine. " + description + "\n\n");

            write("Take 1 down, pass it around, ");
            if (i > 1) {
                write((i - 1) + " bottle(s) of wine on the wall.\n");
            }
            else {
                write("No more bottles of wine on the wall!\n");
            }
        }
    /);

# DIRECTION

v8's interface and behaviour changes a lot. Updating this module to support
newer versions of v8 is a big job. The module currently supports v8 6.2.

The dramatic API changes mean that backward compatibility with the
current API will be effectively impossible. The likelihood of security
holes in a library as large, complex and high-profile as V8 means means it
will be necessary to keep up with the current version, rather than with the
one that this module supports.

Therefore, the next steps will be to use the excellent [Alien::Build](https://metacpan.org/pod/Alien%3A%3ABuild)
to make an "alien" module that builds and makes available v8. The current
[Alien::V8](https://metacpan.org/pod/Alien%3A%3AV8) is not suitable, since its last release was from 2011.

Contributions of effort will be welcome. Please open an RT, or just `#v8`
on `irc.perl.org` to get involved.

Google maintains a public document describing v8's API changes:
[https://docs.google.com/document/d/1g8JFi8T\_oAE\_7uAri7Njtig7fKaPDfotU6huOa1alds/edit#](https://docs.google.com/document/d/1g8JFi8T_oAE_7uAri7Njtig7fKaPDfotU6huOa1alds/edit#)

# INSTALLING V8

## Memory notes

Please note that v8 needs around 2MB of VSZ memory. See
[https://rt.cpan.org/Ticket/Display.html?id=78512](https://rt.cpan.org/Ticket/Display.html?id=78512) for more information.

## From Source

See [https://v8.dev/docs/build](https://v8.dev/docs/build) for how. Be warned, the source repo
alone is over 800MB.

### On OS X

On OS X I've successfully used [Homebrew](http://mxcl.github.com/homebrew/),
install Homebrew then:

    brew install v8

## Binary

### Linux

On Ubuntu 18.04 (and possibly Debian), the library and header files can be installed by running:

    sudo aptitude install libv8-3.14.5 libv8-3.14-dev

Similar packages may be available for other distributions (adjust the package names accordingly).

# SEE ALSO

## Further documentation

- [JavaScript::V8::Context](https://metacpan.org/pod/JavaScript%3A%3AV8%3A%3AContext)

    Details on the context object and the mapping between JavaScript and Perl
    types.

## Extension modules

- [JavaScript::V8x::TestMoreish](https://metacpan.org/pod/JavaScript%3A%3AV8x%3A%3ATestMoreish)

## Other JavaScript bindings for Perl

- [JavaScript](https://metacpan.org/pod/JavaScript)
- [JavaScript::Lite](https://metacpan.org/pod/JavaScript%3A%3ALite)
- [JavaScript::SpiderMonkey](https://metacpan.org/pod/JavaScript%3A%3ASpiderMonkey)
- [JE](https://metacpan.org/pod/JE)

# REPOSITORY

The source code lives at [http://github.com/dgl/javascript-v8](http://github.com/dgl/javascript-v8).

# AUTHORS

    Pawel Murias <pawelmurias at gmail dot com>
    David Leadbeater <dgl@dgl.cx>
    Paul Driver <frodwith at gmail dot com>
    Igor Zaytsev <igor.zaytsev@gmail.com>

# COPYRIGHT AND LICENSE

    Copyright (c) 2009-2010 Paweł Murias
    Copyright (c) 2011 David Leadbeater
    Copyright (c) 2011 Igor Zaytsev

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

# ACKNOWLEDGMENTS

- Claes Jakobsson &lt;claesjac at cpan dot org>

    I stole and adapted pieces of docs and API design from JavaScript.pm

- Brian Hammond &lt;brain @ fictorial dot com>

    For salvaging the code of V8.pm from a message board (which I took some code
    and the idea from)

- The hacker who wrote V8.pm and posted it on the message board

    ([http://d.hatena.ne.jp/dayflower/20080905/1220592409](http://d.hatena.ne.jp/dayflower/20080905/1220592409))

- All the fine people at #perl@freenode.org for helping me write this module
