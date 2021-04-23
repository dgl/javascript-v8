use 5.008;
use ExtUtils::MakeMaker;
use ExtUtils::CppGuess;
use Devel::CheckLib;

my $V8_DIR = $ENV{V8_DIR};

check_lib_or_exit(
    lib => ['v8'],
    $V8_DIR
        ? ( libpath => $V8_DIR, incpath => "$V8_DIR/include" )
        : (),
);

my $guess = ExtUtils::CppGuess->new;

WriteMakefile(
    NAME              => 'JavaScript::V8',
    VERSION_FROM      => 'lib/JavaScript/V8.pm',
    BUILD_REQUIRES    => {
      'ExtUtils::XSpp' => '0.11',
    },
    TEST_REQUIRES     => {
      'Test::Number::Delta' => 0,
    },
    CONFIGURE_REQUIRES => {
      'ExtUtils::MakeMaker' => '6.64', # TEST_REQUIRES
      'ExtUtils::CppGuess' => '0.19',
      'Devel::CheckLib' => '0.92',
    },
    ABSTRACT_FROM  => 'lib/JavaScript/V8.pm',
    AUTHOR         => 'Pawel Murias <pawelmurias@gmail.org>',
    LIBS              => [($V8_DIR ? "-L$V8_DIR " : '') . '-lv8'],
    INC               => '-I.' . ($V8_DIR ? " -I$V8_DIR/include" : ''),
    OBJECT            => '$(O_FILES)', # link all the C files too
    XSOPT             => '-C++ -hiertype',
    TYPEMAPS          => ['perlobject.map'],
    depend            => { 'WithV8Context.c' => 'JavaScript-V8-Context.xsp' },
    META_MERGE => {
      "meta-spec" => { version => 2 },
      resources        => {
        x_IRC => 'irc://irc.perl.org/#v8',
        repository     => {
          type => 'git',
          url => 'git@github.com:dgl/javascript-v8.git',
          web => 'http://github.com/dgl/javascript-v8',
        },
      },
      prereqs => {
        develop => {
          requires => {
            'Test::Pod' => '1.22',
            'Pod::Markdown' => 0,
          },
        },
      },
    },
    $guess->makemaker_options,
);

sub MY::postamble {
  <<EOF;
pure_all :: README.md

README.md : \$(VERSION_FROM)
\tpod2markdown \$< >\$\@
EOF
}
