#include "V8Context.h"

/* Handle Perl < 5.10 */
#if PERL_VERSION < 10
  #define Null(type) ((type)NULL)
#endif

/* We need one MODULE... line to start the actual XS section of the file.
 * The XS++ preprocessor will output its own MODULE and PACKAGE lines */
MODULE = JavaScript::V8		PACKAGE = JavaScript::V8

## The include line executes xspp with the supplied typemap and the
## xsp interface code for our class.
## It will include the output of the xsubplusplus run.

INCLUDE_COMMAND: $^X -MExtUtils::XSpp::Cmd -e xspp -- --typemap=typemap.xsp JavaScript-V8-Context.xsp

