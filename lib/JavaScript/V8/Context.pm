package JavaScript::V8::Context;

sub new {
    my($class, %args) = @_;

    my $time_limit = delete $args{time_limit} || 0;
    my $flags = delete $args{flags} || '';
    my $enable_blessing 
        = exists $args{enable_blessing} 
        ? delete $args{enable_blessing} 
        : (exists $args{bless_prefix} ? 1 : 0);
    my $bless_prefix = delete $args{bless_prefix} || '';

    $class->_new($time_limit, $flags, $enable_blessing, $bless_prefix);
}

sub bind_function {
    my $class = shift;
    $class->bind(@_);
}

1;

=head1 NAME

JavaScript::V8::Context - An object in which we can execute JavaScript

=head1 SYNOPSIS

  use JavaScript::V8;

  # Create a runtime and a context
  my $context = JavaScript::V8::Context->new();

  # Add a function which we can call from JavaScript
  $context->bind_function(print => sub { print @_ });

  my $result = $context->eval($source);

=head1 INTERFACE

=over

=item new ( %parameters )

Create a new JavaScript::V8::Context object.

Several optional parameters are accepted:

=over

=item time_limit

Force an exception after the script has run for a number of seconds; this
limit will be enforced even if V8 calls back to Perl or blocks on IO.

=item enable_blessing

If enabled, JavaScript objects that have the C<__perlPackage> property are
converted to Perl blessed scalar references. These references are blessed
into a Perl package with a name C<bless_prefix> + C<__perlPackage>. This
package is automagically created and filled with methods from JavaScript
object prototype. C<bless_prefix> is optional and can be left out if you
completely trust the JavaScript code you're running.

=item bless_prefix

Specifies a package name prefix to use for blessed JavaScript objects. Has
no effect unless C<enable_blessing> is set.

=item flags

Specify a string of flags to be passed to V8. See
C<set_flags_from_string()> for more details.

=back

=item bind ( name => $scalar )

Converts the given scalar value (array ref, code ref, or hash ref) to a V8
value and binds it in this execution context.

Examples:

=over

=item Functions

Pass the bind method the name of a function, and a corresponding code
reference to have the name bound to the Perl code. For example:

  $context->bind(hello => sub { print shift, "\n" });
  $context->eval("hello('Hello from JavaScript')");

=item Basic objects

Pass the bind method the name of an object, and a hash reference containing
method names and corresponding code references, for example:

  $context->bind(
    test_object => {
      method => sub { return shift },
    },
  );

  print $context->eval("test_object.method('Hello')");

=item Blessed objects

Pass the bind method the name of an object, and a blessed reference, for
example:

  $context->bind(
    perl_object => $blessed_reference,
  );

  print $context->eval("perl_object.method('Hello')");

B<This requires Perl 5.10 or later.>

=item Arrays

Pass the bind method a JavaScript variable name with an array reference to
have the name bound to the Perl array reference. Note that this binding is
one way; JavaScript can access Perl arrays and manipulate them, but changes
are not made available to the calling Perl code. Similarly, a JavaScript
array cannot be manipulated directly in Perl. Example:

  my $test_array_ref = ["hello"];

  $context->bind(
    test_array => $test_array_ref,
  );

  print $context->eval("test_array.pop()");

Calling the JavaScript pop method does not alter C<$test_array_ref> in
Perl.

=item Hashes

Pass the bind method a JavaScript variable name with a hash reference to
have the name bound to the Perl hash reference. For example:

  $context->bind(
    window => {
      location => "http://perl.org",
    },
  );

  print $context->eval("window.location");

This creates the "window.location" property in JavaScript. Properties can
be changed in JavaScript, but do not change the corresponding Perl data
structure.

=back

The exact semantics of this interface are subject to change in a future
version (the binding may become more complete).

=item bind_function ( $name => $subroutine_ref )

DEPRECATED. This is just an alias for bind.

=item eval ( $source[, $origin] )

Evaluates the JavaScript code given in I<$source> and
returns the result from the last statement.

C<JavaScript::V8> attempts to convert the return value to the corresponding
Perl type:

    JavaScript return type    |   Type returned by eval()
  ----------------------------+----------------------------
  Undefined, null             | undef
  Numeric                     | scalar
  Boolean                     | scalar (1 or 0)
  String                      | scalar
  Function                    | code reference
  Object                      | hash reference or blessed scalar reference
  Array                       | array reference

If there is a compilation error (such as a syntax error) or an uncaught
exception is thrown in JavaScript, this method returns undef and $@ is set.
If an optional origin for C<$source> has been provided, this will be
reported as the origin of the error in $@. This is useful for debugging
when eval-ing code from multiple different files or locations.

A function reference returned from JavaScript is not wrapped in the context
created by eval(), so JavaScript exceptions will propagate to Perl code.

JavaScript function object having a C<__perlReturnsList> property set that
returns an array will return a list to Perl when called in list context.

=item set_flags_from_string ( $flags )

Set or unset various flags supported by V8 (see
L<http://code.google.com/p/v8/source/browse/trunk/src/flag-definitions.h>
or F<src/flag-definitions.h> in the V8 source for details of all available
flags).

For example, the C<builtins_in_stack_traces> flag controls showing built-in
functions in stack traces. To set this, call:

  $context->set_flags_from_string("--builtins-in-stack-traces");

Note underscores are replaced with hyphens. Note that flags which are
enabled by default are disabled by prefixing the name with "no" - for
example, the "foo" flag could be disabled with C<--nofoo>.

Flags are commonly used for debugging or changing the behaviour of V8 in
some way. Some flags can only be set whenever a context is created - set
these with the flags parameter to C<new()>.

=item idle_notification( )

Used as a hint to tell V8 that your application is idle, so now might be a
suitable time for garbage collection. Returns 1 if there is no further work
V8 can currently do.

Most users of C<JavaScript::V8> will not need this.

=back

=cut
