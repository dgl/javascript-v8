package JavaScript::V8::Context;
use Devel::Size;

sub bind_function {
    my $class = shift;
    $class->bind(@_);
}

# Called internally to create coderef that will call a v8 function
sub _make_v8_caller {
    my ($self, $ptr) = @_;
    return sub { $self->_call_v8_function($ptr, \@_) };
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

=item bind ( name => $scalar )

Converts the given scalar value (array ref, code ref, or hash ref) to a v8
value and binds it in this context.

=item bind_function ( $name => $subroutine_ref )

DEPRECATED. This is just an alias for bind.

=item eval ( $source )

Evaluates the JavaScript code given in I<$source> and
returns the result from the last statement.

If there is a compilation error (such as a syntax error) or an uncaught exception
is thrown in JavaScript, this method returns undef and $@ is set.

=back

=cut
