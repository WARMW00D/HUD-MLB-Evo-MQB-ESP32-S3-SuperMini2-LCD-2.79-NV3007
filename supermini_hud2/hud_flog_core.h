/*
  hud_flog_core.h — кольцевой журнал во внешней флеши NOR (W25Q128), без файловой системы
  ------------------------------------------------------------------------------
  Область — FLOG_SLOTS слотов по FLOG_SLOT_BYTES в конце флеши. Один слот = один «файл» hud.log.NNNN
  (NNNN = 0000..9999 по кругу; слот = NNNN % 4, поэтому переход 9999 -> 0000 не ломает порядок слотов).

  Слот (512 КБ = 128 секторов по 4 КБ):
    сектор 0: заголовок 16 байт  "HLG1", seq(2), ~seq(2), start_epoch(4; FFFFFFFF — время не было известно), FF*4,
              затем текст; сектор k>=1: тег 4 байта 'S', seq(2), 0xA5, затем текст.
    Сектора стираются «лениво», перед первой записью в них; устаревшие данные предыдущего файла отсекаются тегом seq.
    Конец файла находится после перезагрузки: последний сектор с тегом этого seq + первый байт 0xFF (в тексте
    байта 0xFF нет: при записи он заменяется на '?').
  Заполненный слот закрывается, следующий (seq+1) начинает работу: его сектор 0 стирается, старый файл исчезает.
  Таким образом хранится не больше FLOG_SLOTS файлов (самый старый затирается при ротации).
*/
#ifndef HUD_FLOG_CORE_H
#define HUD_FLOG_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* значения по умолчанию, если в user_config.h (старая копия) их ещё нет */
#ifndef HUD_FLASH_LOG
#define HUD_FLASH_LOG 1
#endif
#ifndef HUD_FLASH_LOG_HEARTBEAT_S
#define HUD_FLASH_LOG_HEARTBEAT_S 60
#endif

#define FLOG_SLOTS         4u
#define FLOG_SECTOR        4096u
#define FLOG_SLOT_BYTES    (512u * 1024u)
#define FLOG_SECTORS       (FLOG_SLOT_BYTES / FLOG_SECTOR)   /* 128 */
#define FLOG_TOTAL_BYTES   (FLOG_SLOTS * FLOG_SLOT_BYTES)    /* 2 МБ */
#define FLOG_SEQ_MOD       10000u
#define FLOG_HDR_BYTES     16u
#define FLOG_TAG_BYTES     4u
#define FLOG_EPOCH_UNKNOWN 0xFFFFFFFFu

typedef struct {
    bool (*read)(uint32_t addr, uint8_t *dst, size_t n, void *user);
    bool (*erase)(uint32_t addr, void *user);                          /* сектор 4 КБ */
    bool (*write)(uint32_t addr, const uint8_t *src, size_t n, void *user); /* n <= 256, внутри страницы */
    void *user;
} flog_io_t;

typedef struct {
    uint16_t seq;
    uint8_t  slot;
    uint32_t size;          /* байт текста */
    uint32_t start_epoch;   /* 0 — неизвестно */
} flog_file_t;

typedef struct {
    flog_io_t io;
    uint32_t  base;                 /* адрес области во флеши, кратен 4096 */
    uint16_t  seq;                  /* текущий файл */
    uint8_t   slot;
    uint16_t  sector;               /* текущий сектор слота 0..127 */
    uint16_t  off;                  /* смещение записи в секторе */
    uint32_t  start_epoch;          /* значение в заголовке текущего файла (FLOG_EPOCH_UNKNOWN) */
    uint8_t   sec[FLOG_SECTOR];     /* рабочий буфер */
} flog_t;

/* Прочитать область, найти текущий файл и конец записи; пустую область форматирует (hud.log.0000). */
bool   flog_mount(flog_t *f, const flog_io_t *io, uint32_t base);
/* Дописать текст. Возвращает, сколько байт записано (меньше n — ошибка флеши: вызывающий делает flog_mount заново).
   epoch_now — для заголовка нового файла при ротации (0 — неизвестно). */
size_t flog_append(flog_t *f, const uint8_t *data, size_t n, uint32_t epoch_now);
/* Записать время начала в заголовок текущего файла, если оно было неизвестно. */
bool   flog_set_epoch_if_unknown(flog_t *f, uint32_t epoch);
/* Файлы от самого старого к новому; возвращает их число. */
int    flog_list(flog_t *f, flog_file_t out[FLOG_SLOTS]);
/* Прочитать текст файла (off + n <= size). */
bool   flog_read(flog_t *f, const flog_file_t *file, uint32_t off, uint8_t *dst, size_t n);
/* true, если заголовок слота по-прежнему принадлежит файлу (его не затёрла ротация). */
bool   flog_still_valid(flog_t *f, const flog_file_t *file);

/* ---- выгрузка .tar: hud.log.NNNN, от старого к новому ---- */
typedef struct {
    flog_file_t files[FLOG_SLOTS];
    int      n, idx, phase;
    uint32_t off;
    uint32_t total;
    bool     err;
    uint8_t  hdr[512];
} flog_tar_t;

void     flog_tar_begin(flog_t *f, flog_tar_t *t);          /* перечислить файлы, посчитать размер */
uint32_t flog_tar_total(const flog_tar_t *t);                /* 0 — файлов нет */
size_t   flog_tar_read(flog_t *f, flog_tar_t *t, uint8_t *dst, size_t n);   /* следующие n байт потока; 0 — конец */

#ifdef __cplusplus
}
#endif
#endif
