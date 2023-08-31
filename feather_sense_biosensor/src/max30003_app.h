/*
 * max30003_app.h
 *
 *  Created on: Jun 20, 2018
 *      Author: Mahir.Ozturk
 */

#ifndef MAX30003_APP_H_
#define MAX30003_APP_H_


struct max30003_reader_task_args {
	Thread *self;
	SPI &spiBus;
	PinName spiCS;
};

void max30003_reader_task(struct max30003_reader_task_args *args);

#endif /* MAX30003_APP_H_ */
