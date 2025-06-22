#include "logger.h"
#include <stdio.h>

// TODO eventually make this it's own module since it'll prolly be needed elsewhere

/////////////////////////////////// LOGPRINT TODO maybe move this chunk into its own file? or module?
bool logprintTags[LOGPRINT_MAX] = // TODO this can be a bitmap if you really want to deal with it
  {
    [INFO] = true,
    [VERBOSE] = false,
    [ERROR] = true,
    [DEBUG] = false 
  };

const char logprintTagChars[LOGPRINT_MAX] = 
  {
    [INFO] = 'I',
    [VERBOSE] = 'V',
    [ERROR] = 'E',
    [DEBUG] = 'D'
  };

void _logprint(LogprintTag_e tag, const char* format, ... )
{
  if (!logprintTags[tag])
  {
    return;
  }
  va_list args;
  va_start( args, format );
  printf("[IPERF][%c] ", logprintTagChars[tag]);
  vprintf( format, args );
  va_end( args );
}
