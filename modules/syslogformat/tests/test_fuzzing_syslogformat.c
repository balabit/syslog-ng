
#include <stdint.h>
#include <stddef.h>

#include "apphook.h"
#include "msg-format.h"
#include "logmsg/logmsg.h"
#include "cfg.h"
#include "syslog-format.h"
#include <iv.h>


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size <= 1) return 0;

  GlobalConfig *cfg = cfg_new_snippet();
  app_startup();
  cfg_load_module(cfg, "syslogformat");
  MsgFormatOptions parse_options;
  msg_format_options_defaults(&parse_options);

  LogMessage *msg = log_msg_new_empty();

  syslog_format_handler(&parse_options, data, size, msg);

  log_msg_unref(msg);
  cfg_free(cfg);
  app_shutdown();
  iv_deinit();
  return 0;
}



