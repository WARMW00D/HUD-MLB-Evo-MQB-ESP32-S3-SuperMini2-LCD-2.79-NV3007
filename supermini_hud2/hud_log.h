#ifndef HUD_LOG_H
#define HUD_LOG_H
#ifdef __cplusplus
extern "C" {
#endif
/* Serial + журнал во флеше W25Q128 (hud_flog.h). Текст со своим переводом строки; метки времени ставит журнал. */
void hud_log_write(const char *s);
void arduino_printf(const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif
