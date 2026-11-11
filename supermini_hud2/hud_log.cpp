#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "hud_log.h"
#include "hud_flog.h"

/* Serial + журнал во флеше (hud_flog.cpp) */
void hud_log_write(const char *s) { Serial.print(s); hud_flog_push(s); }
void arduino_printf(const char *format, ...)
{
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    hud_log_write(buf);
}
