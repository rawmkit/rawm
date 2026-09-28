#ifndef RAWM_TEST_STATUS_ALSA_FAKE_H
#define RAWM_TEST_STATUS_ALSA_FAKE_H

void rawm_test_alsa_reset(void);
void rawm_test_alsa_set_poll_count(int count);
long rawm_test_alsa_left_volume(void);
long rawm_test_alsa_right_volume(void);
int rawm_test_alsa_muted(void);
int rawm_test_alsa_handled_events(void);

#endif
