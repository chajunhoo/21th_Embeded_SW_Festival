/*
 * max30101_app.cpp
 *
 *  Created on: Jun 20, 2018
 *      Author: Mahir.Ozturk
 */
#include <mbed.h>
#include <max30101_algo.h>
#include <max30101_app.h>
#include <MAX30101.h>
#include <rtos.h>
#include <data.h>

#define MAX30101_IRQ_ASSERTED_ID	1

#define MAX30101_BUFFER_LEN			500

static Thread *thread = 0;
EventFlags max30101_irq_flags;

CircularBuffer<ppg_msg_t, MAX_BUF_SIZE> ppg_buf;

//variable for the algorithm
uint16_t sampleRate = 100;
uint16_t compSpO2 = 1;
int16_t ir_ac_comp = 0;
int16_t red_ac_comp = 0;
int16_t green_ac_comp = 0;
int16_t ir_ac_mag = 0;
int16_t red_ac_mag = 0;
int16_t green_ac_mag = 0;
uint16_t HRbpm2 = 0;
uint16_t SpO2B = 0;
uint16_t DRdy = 0;

//declare large variables outside of main
uint32_t redData[MAX30101_BUFFER_LEN];//set array to max fifo size
uint32_t irData[MAX30101_BUFFER_LEN];//set array to max fifo size
uint32_t greenData[MAX30101_BUFFER_LEN];//set array to max fifo size

bool max30101_config(MAX30101 &op_sensor)
{
	//Reset Device
	MAX30101::ModeConfiguration_u modeConfig;
	modeConfig.all = 0;
	modeConfig.bits.reset = 1;
	modeConfig.bits.mode = MAX30101::MultiLedMode;     // Sets SPO2 Mode
	int32_t rc = op_sensor.setModeConfiguration(modeConfig);

	//enable MAX30101 interrupts
	MAX30101::InterruptBitField_u ints;
	if (rc == 0) {
		ints.all = 0;
		ints.bits.a_full = 1;       // Enable FIFO almost full interrupt
		ints.bits.ppg_rdy =1;       //Enables an interrupt when a new sample is ready
		rc = op_sensor.enableInterrupts(ints);
	}

	//configure FIFO
	MAX30101::FIFO_Configuration_u fifoConfig;
	if (rc == 0) {
		fifoConfig.all = 0;
		fifoConfig.bits.fifo_a_full = 10;                            // Max level of 17 samples
		fifoConfig.bits.sample_average = MAX30101::AveragedSamples_0;// Average 0 samples
		rc = op_sensor.setFIFOConfiguration(fifoConfig);
	}

	MAX30101::SpO2Configuration_u spo2Config;
	if (rc == 0) {
		spo2Config.all = 0;                                 // clears register
		spo2Config.bits.spo2_adc_range = 1;                 //sets resolution to 4096 nAfs
		spo2Config.bits.spo2_sr = MAX30101::SR_100_Hz;     // SpO2 SR = 100Hz
		spo2Config.bits.led_pw = MAX30101::PW_3;            // 18-bit ADC resolution ~400us
		rc = op_sensor.setSpO2Configuration(spo2Config);
	}

	//Set time slots for LEDS
	MAX30101::ModeControlReg_u multiLED;
	if (rc == 0) {
		//sets timing for control register 1
		multiLED.bits.lo_slot=1;
		multiLED.bits.hi_slot=2;
		rc = op_sensor.setMultiLEDModeControl(MAX30101::ModeControlReg1, multiLED);
		if (rc == 0) {
			multiLED.bits.lo_slot=3;
			multiLED.bits.hi_slot=0;
			rc = op_sensor.setMultiLEDModeControl(MAX30101::ModeControlReg2, multiLED);
		}
	}

	//Set LED drive currents
	if (rc == 0) {
		// Heart Rate only, 1 LED channel, Pulse amp. = ~7mA
		rc = op_sensor.setLEDPulseAmplitude(MAX30101::LED1_PA, 0x24);
		//To include SPO2, 2 LED channel, Pulse amp. ~7mA
		if (rc == 0) {
			rc = op_sensor.setLEDPulseAmplitude(MAX30101::LED2_PA, 0x24);
		}
		if (rc == 0) {
			rc = op_sensor.setLEDPulseAmplitude(MAX30101::LED3_PA, 0x24);
		}
	}

	//Set operating mode
	modeConfig.all = 0;
	if (rc == 0) {
		modeConfig.bits.mode = MAX30101::MultiLedMode;     // Sets multiLED mode
		rc = op_sensor.setModeConfiguration(modeConfig);
	}

	return rc;
}

void max30101wing_pmic_config(I2C & i2c_bus, DigitalOut & pmic_en)
{
	const uint8_t PMIC_ADRS = 0x54;
	const uint8_t BBB_EXTRA_ADRS = 0x1C;
	const uint8_t BOOST_VOLTAGE = 0x05;

	char data_buff[] = {BBB_EXTRA_ADRS, 0x40};    //BBBExtra register address
	//and data to enable passive
	//pull down.
	i2c_bus.write(PMIC_ADRS, data_buff,2);        //write to BBBExtra register

	data_buff[0] = BOOST_VOLTAGE;
	data_buff[1] = 0x08;                          //Boost voltage configuration
	//register followed by data
	//to set voltage to 4.5V 1f
	pmic_en = 0;                                  //disables VLED 08
	i2c_bus.write(PMIC_ADRS, data_buff,2);        //write to BBBExtra register
	pmic_en = 1;                                  //enables VLED
}

/* Op Sensor FIFO nearly full callback */
void max30101_intr_callback()
{
	if (thread != 0) {
		//thread->signal_set(MAX30101_IRQ_ASSERTED_ID);
		max30101_irq_flags.set(MAX30101_IRQ_ASSERTED_ID);
	}
}

void max30101_reader_task(struct max30101_reader_task_args *args)
{
	InterruptIn op_sensor_int(args->pinIntr);		// Config P3_2 as int. in for
	op_sensor_int.fall(max30101_intr_callback);		// FIFO ready interrupt

	DigitalOut VLED_EN(args->pinVLED,0);			//Enable for VLEDs
	max30101wing_pmic_config(args->i2cBus, VLED_EN);

	MAX30101 op_sensor(args->i2cBus);				// Create new MAX30101 on i2cBus
	int rc = max30101_config(op_sensor);			// Config sensor, return 0 on success

	MAX30101::InterruptBitField_u ints;				// Read interrupt status to clear
	rc = op_sensor.getInterruptStatus(ints);		// power on interrupt

	thread = args->self;

	uint8_t fifoData[MAX30101::MAX_FIFO_BYTES];
	uint16_t idx, readBytes;
	uint16_t HRTemp;
	uint16_t spo2Temp;

	uint16_t lastValidHR = 0;
	uint16_t lastValidSPO2 = 0;

	int r=0; //counter for redData position
	int ir=0; //counter for irData position
	int g =0; //counter for greenData position
	int c=0; //counter to print values

	int consecCalcFailCnt = 0;

	ppg_msg_t ppg_data;

	//printf("Starting MAX30101 HeartRate / SPO2 Demo Application...\r\n");
	//printf("Please wait a few seconds while data is being collected.\r\n");

	//Timer bleNotifyTimer;

	//bleNotifyTimer.start();

	while (1) {
		if (rc == 0) {
			/* Check if op_sensor interrupt asserted */
			//thread->signal_wait(MAX30101_IRQ_ASSERTED_ID);
			max30101_irq_flags.wait_all(MAX30101_IRQ_ASSERTED_ID);

			/* Read interrupt status to clear interrupt */
			rc = op_sensor.getInterruptStatus(ints);

			/* Confirms proper read prior to executing */
			if (rc == 0) {
				// Read FIFO
				rc = op_sensor.readFIFO(MAX30101::ThreeLedChannels, fifoData, readBytes);

				if (rc == 0) {
					/* Convert read bytes into samples */
					for (idx = 0; idx < readBytes; idx+=9) {
						if (r >= 500 || ir >= 500 || g >= 500) {
							//printf("Overflow! r=%d ir=%d g=%d\r\n", r, ir, g);
							break;
						}

						if (readBytes >= (idx + 8)) {
							redData[r++] = ((fifoData[idx] << 16) | (fifoData[idx + 1] << 8) | (fifoData[idx + 2])) & 0x03FFFF;
							irData[ir++] = ((fifoData[idx + 3] << 16) | (fifoData[idx + 4] << 8) | (fifoData[idx + 5])) & 0x03FFFF;
							greenData[g++] = ((fifoData[idx + 6] << 16) | (fifoData[idx + 7] << 8) | (fifoData[idx + 8])) & 0x03FFFF;
						}

					}

					if ((r >= MAX30101_BUFFER_LEN) && (ir >= MAX30101_BUFFER_LEN) && (g >= MAX30101_BUFFER_LEN)) {/* checks to make sure there are 500 */
						/* samples in data buffers */

						/* runs the heart rate and SpO2 algorithm */
						for (c = 0, HRTemp = 0; c < r; c++) {
							HRSpO2Func(irData[c], redData[c],(greenData[c]<<3), c,sampleRate, compSpO2,
									   &ir_ac_comp,&red_ac_comp, &green_ac_comp, &ir_ac_mag,&red_ac_mag,
									   &green_ac_mag, &HRbpm2,&SpO2B,&DRdy);
							if (DRdy) {
								HRTemp = HRbpm2;
								spo2Temp = SpO2B;
							}
							//printf("I:%d, R:%d, G:%d\n",irData[c], redData[c], greenData[c]);
							
							//sprintf(ppgString, "IR:%d",irData[c]);
            				//strcpy(sensor_data->data, ppgString);
            				//sensorFifo.put(sensor_data);
						}

						/* If the above algorithm returns a valid heart rate on the last sample, it is printed */
						if (DRdy == 1) {
							ppg_data.hr = HRbpm2;
							ppg_data.spo2 = SpO2B;
							ppg_buf.push(ppg_data);
							// sprintf(ppgString, "spO2:%d",SpO2B);
            				// strcpy(sensor_data->data, ppgString);
            				// sensorFifo.put(sensor_data);
							lastValidHR = HRbpm2;
							lastValidSPO2 = SpO2B;
							consecCalcFailCnt = 0;
						} else if (HRTemp != 0) { /* if a valid heart was calculated at all, it is printed */
							ppg_data.hr = HRTemp;
							ppg_data.spo2 = spo2Temp;
							ppg_buf.push(ppg_data);
							// sprintf(ppgString, "spO2:%d",spo2Temp);
            				// strcpy(sensor_data->data, ppgString);
            				// sensorFifo.put(sensor_data);
							lastValidHR = HRTemp;
							lastValidSPO2 = spo2Temp;
							consecCalcFailCnt = 0;
						} else {
							consecCalcFailCnt++;
							if (consecCalcFailCnt >= 10) {
								//printf("Calculation failed...waiting for more samples...\r\n");
								//printf("Please keep your finger on the MAX30101 sensor with minimal movement.\r\n");
								consecCalcFailCnt = 0;
							}
						}

						/* dump the first hundred samples after calculation */
						for (c = 100; c < 500; c++) {
							redData[c - 100] = redData[c];
							irData[c - 100] = irData[c];
							greenData[c - 100] = greenData[c];
						}

						/* reset counters */
						r = 400;
						ir = 400;
						g = 400;
					}

					// if (bleNotifyTimer.read_ms() >= (args->notify_period_sec * 1000)) {
					// 	bleGattAttrWrite(args->gattHeartRate, (uint8_t *)&lastValidHR, sizeof(lastValidHR));
					// 	bleGattAttrWrite(args->gattSPO2, (uint8_t *)&lastValidSPO2, sizeof(lastValidSPO2));
					// 	bleNotifyTimer.reset();
					// }
				}
			}
		} else { // If rc != 0, a communication error has occurred

			//printf("Something went wrong, "
			//		  "check the I2C bus or power connections... \r\n");

			return;
		}

	}
}


