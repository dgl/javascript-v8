use 5.008;
use ExtUtils::MakeMaker;

use lib qw(inc);
use Devel::CheckLib;

check_lib_or_exit(lib => ['v8']);

my $CC = 'g++';
my %mm = (
    NAME              => 'JavaScript::V8',
    VERSION_FROM      => 'lib/JavaScript/V8.pm', # finds $VERSION
    PREREQ_PM         => {
      'ExtUtils::XSpp' => '0.11',
      'Devel::Size'    => '0.71',
    }, # e.g., Module::Name => 1.1
    ABSTRACT_FROM  => 'lib/JavaScript/V8.pm', # retrieve abstract from module
    AUTHOR         => 'Pawel Murias <pawelmurias@gmail.org>',
    LIBS              => ['-lv8'], # e.g., '-lm'
    DEFINE            => '', # e.g., '-DHAVE_SOMETHING'
    INC               => '-I.', # e.g., '-I. -I/usr/include/other'
    OBJECT            => '$(O_FILES)', # link all the C files too
    XSOPT             => '-C++ -hiertype',
    TYPEMAPS          => ['perlobject.map'],
    CC                => $CC,
    LD                => '$(CC)',
);

if(ExtUtils::MakeMaker->can("VERSION") && ExtUtils::MakeMaker->VERSION(6.46)) {
  $mm{META_MERGE} = {
    resources        => {
      repository     => 'http://github.com/dgl/javascript-v8',
    }
  };
}

WriteMakefile(%mm);
