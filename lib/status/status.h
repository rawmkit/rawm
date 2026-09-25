#ifndef RAWM_STATUS_H
#define RAWM_STATUS_H

#include <stddef.h>

#define RAWM_STATUS_V2_MAX 4096u
#define RAWM_STATUS_ID_MAX 64u

typedef enum {
  RAWM_STATUS_NORMAL,
  RAWM_STATUS_WARNING,
  RAWM_STATUS_CRITICAL
} RawmStatusState;

typedef struct {
  char *id;
  char *text;
  RawmStatusState state;
} RawmStatusSegment;

typedef struct {
  RawmStatusSegment *segments;
  size_t count;
} RawmStatusData;

typedef struct {
  const char *id;
  const char *text;
  RawmStatusState state;
} RawmStatusView;

typedef enum {
  RAWM_STATUS_OK = 0,
  RAWM_STATUS_INVALID,
  RAWM_STATUS_TOO_LARGE,
  RAWM_STATUS_NO_MEMORY
} RawmStatusResult;

int rawm_status_utf8_valid(const unsigned char *text, size_t length);
int rawm_status_id_valid(const unsigned char *id, size_t length);
int rawm_status_printable_utf8_valid(const unsigned char *text, size_t length);
int rawm_status_text_valid(const unsigned char *text, size_t length);
const char *rawm_status_state_name(RawmStatusState state);
int rawm_status_state_parse(const unsigned char *text, size_t length,
                            RawmStatusState *state);
void rawm_status_data_destroy(RawmStatusData *status);
RawmStatusResult rawm_status_v2_parse(const unsigned char *value, size_t length,
                                      RawmStatusData *status);
RawmStatusResult rawm_status_v2_encode(const RawmStatusView *segments,
                                       size_t count, char **value,
                                       size_t *length);

#endif
