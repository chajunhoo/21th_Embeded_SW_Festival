/*
 * max30101_app.h
 *
 *  Created on: Jun 20, 2018
 *      Author: Mahir.Ozturk
 */

#ifndef MAX30101_APP_H_
#define MAX30101_APP_H_
#include <mbed.h>

struct max30101_reader_task_args {
	Thread *self;
	I2C &i2cBus;
	PinName pinIntr;
	PinName pinVLED;
};

void max30101_reader_task(struct max30101_reader_task_args *args);

#endif /* MAX30101_APP_H_ */
