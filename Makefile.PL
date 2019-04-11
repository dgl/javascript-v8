use 5.008;
use ExtUtils::MakeMaker;
use ExtUtils::CppGuess;

use lib qw(inc);
use Devel::CheckLib;

my $V8_DIR = $ENV{V8_DIR};

check_lib_or_exit(
    lib => ['v8'],
    $V8_DIR
        ? ( libpath => $V8_DIR, incpath => "$V8_DIR/include" )
        : (),
);

my $guess = ExtUtils::CppGuess->new;
$guess->add_extra_compiler_flags('-fpermissive') if gcc_version('g++') >= 4.5;
my %mm = (
    NAME              => 'JavaScript::V8',
    VERSION_FROM      => 'lib/JavaScript/V8.pm', # finds $VERSION
    BUILD_REQUIRES    => {
      'ExtUtils::XSpp' => '0.11',
    },
    TEST_REQUIRES     => {
      'Test::Number::Delta' => 0,
    },
    CONFIGURE_REQUIRES => {
      'ExtUtils::MakeMaker' => '6.64', # TEST_REQUIRES
      'ExtUtils::CppGuess' => '0.19',
    },
    ABSTRACT_FROM  => 'lib/JavaScript/V8.pm', # retrieve abstract from module
    AUTHOR         => 'Pawel Murias <pawelmurias@gmail.org>',
    LIBS              => [($V8_DIR ? "-L$V8_DIR " : '') . '-lv8'], # e.g., '-lm'
    DEFINE            => '', # e.g., '-DHAVE_SOMETHING'
    INC               => '-I.' . ($V8_DIR ? " -I$V8_DIR/include" : ''), # e.g., '-I. -I/usr/include/other'
    OBJECT            => '$(O_FILES)', # link all the C files too
    XSOPT             => '-C++ -hiertype',
    TYPEMAPS          => ['perlobject.map'],
    depend            => { 'WithV8Context.c' => 'JavaScript-V8-Context.xsp' },
    META_MERGE => {
      resources        => {
        repository     => 'http://github.com/dgl/javascript-v8',
      }
    },
    $guess->makemaker_options,
);

WriteMakefile(%mm);

sub gcc_version {
  my($cc) = @_;
  my $gcc_out = qx{$cc -v 2>&1};
  # Just the first two digits
  $gcc_out =~ /gcc version (\d+\.\d+)/ ? $1 : 0;
}
