
#ifndef DATA_H_
#define DATA_H_
#include <mbed.h>

#define MAX_BUF_SIZE 10


typedef struct {
    uint16_t hr;
	uint16_t spo2;
} ppg_msg_t;

typedef struct {
	uint16_t nsamples;
	int16_t ecg_sample[32];
} ecg_msg_t;

typedef struct {
    float acc_x;
	float acc_y;
	float acc_z;
	float acc_amp;
} acc_msg_t;

#endif /* MAX30101_APP_H_ */
