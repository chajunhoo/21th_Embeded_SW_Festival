#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include <mbed.h>
#include <max32630fthr.h>
#include <bmi160.h>
#include <USBSerial.h>
#include <platform/mbed_thread.h>
#include <MAX30101.h>
#include <max30101_app.h>
#include <max30003_app.h>
#include <data.h>
#include <drivers/Ticker.h>
#include <platform/CircularBuffer.h>

using namespace mbed;

MAX32630FTHR pegasus(MAX32630FTHR::VIO_3V3);

DigitalOut rLED(LED1);
DigitalOut gLED(LED2);
DigitalOut bLED(LED3);


I2C i2cBus(P5_7, P6_0);
BMI160_I2C imu(i2cBus, BMI160_I2C::I2C_ADRS_SDO_LO);

I2C i2cBus2(P3_4, P3_5);

SPI spim2(SPI2_MOSI, SPI2_MISO, SPI2_SCK);

Thread accelThread;
Thread ppgThread;
Thread ecgThread;

Ticker accelTicker;

Ticker onesecTicker;

// Status LED set/clear masks (fixed)
//
#define ST_STARTUP  0x01
#define ST_GPS_FIX  0x02
#define ST_USB_PWR  0x04
#define ST_RECORD   0x10
#define ST_MSD      0x20
#define ST_ERROR    0x80

void SetStatusLed(uint8_t f);
void ClearStatusLed(uint8_t f);
void UpdateStatusLed();


CircularBuffer<acc_msg_t, MAX_BUF_SIZE> acc_buf;
extern CircularBuffer<ppg_msg_t, MAX_BUF_SIZE> ppg_buf;
extern CircularBuffer<ecg_msg_t, MAX_BUF_SIZE> ecg_buf;
extern CircularBuffer<float, MAX_BUF_SIZE> ecg_hr_buf;

// =============================================================================
// Global Variables
// =============================================================================

//
// RGB Status LED colors
//
enum LedColors
{
    BLACK,
    RED,
    YELLOW,
    GREEN,
    CYAN,
    BLUE,
    MAGENTA,
    WHITE
};


//
// Indicies into sensor-related control arrays
//
enum SensorSelect
{
    ACCEL = 0,
    ONESEC,
    GPS,
    LIGHT,
    TEMP
};

uint8_t ledStatus = 0x00;
bool enSnsTick[5];             // Set when time for a sensor to log or record info


void AccelProcess()
{
    BMI160::AccConfig accConfig;
    BMI160::SensorData accData;
    BMI160::SensorTime sensorTime;
    
    bool initSuccess = true;   // Will be set false if necessary

    acc_msg_t acc_data;
    
    // Setup accelerometer configuration
    accConfig.range = BMI160::SENS_16G;
    accConfig.us = BMI160::ACC_US_OFF;
    accConfig.bwp = BMI160::ACC_BWP_2;
    accConfig.odr = BMI160::ACC_ODR_8;
    
    // Initialize
    if (imu.setSensorPowerMode(BMI160::GYRO, BMI160::NORMAL) != BMI160::RTN_NO_ERROR) {
        printf("Failed to set gyro power mode\n\r");
        initSuccess = false;
    }
    thread_sleep_for(100);
    if (imu.setSensorPowerMode(BMI160::ACC, BMI160::NORMAL) != BMI160::RTN_NO_ERROR) {
        printf("Failed to set accelerometer power mode\n\r");
        initSuccess = false;
    }
    thread_sleep_for(100);
    if (initSuccess){
        if (imu.setSensorConfig(accConfig) != BMI160::RTN_NO_ERROR) {
            printf("Failed to reconfigure accelerometer\n\r");
            initSuccess = false;
        }
    }
    
    if (!initSuccess) {
        SetStatusLed(ST_ERROR);
    } else {
        // Main loop
        while (1) {
            // Wait to be triggered
            while (!enSnsTick[ACCEL]) {
                ThisThread::yield();
            }
            enSnsTick[ACCEL] = false;
            
            // Read accelerometer
            (void) imu.getSensorXYZandSensorTime(accData, sensorTime, accConfig.range);
            
            acc_data.acc_x = accData.xAxis.scaled;
            acc_data.acc_y = accData.yAxis.scaled;
            acc_data.acc_z = accData.zAxis.scaled;
            acc_data.acc_amp = accData.xAxis.scaled*accData.xAxis.scaled
             + accData.yAxis.scaled*accData.yAxis.scaled
             + accData.zAxis.scaled*accData.zAxis.scaled;
            acc_buf.push(acc_data);
            
        }
    }
}

void AccelTick()
{
    enSnsTick[ACCEL] = true;
}

void OnesecTick()
{
    enSnsTick[ONESEC] = true;
}

int main()
{
    // Indicate startup
    ThisThread::sleep_for(1s);
    SetStatusLed(ST_STARTUP);

    i2cBus.frequency(400000);

    for (int i=0; i<5; i++) {
    enSnsTick[i] = false;
    }

    accelThread.start(callback(AccelProcess));
    accelTicker.attach(&AccelTick, 10ms);
    onesecTicker.attach(&OnesecTick, 1s);

    osStatus status;
    struct max30101_reader_task_args args_max30101 = {
        &ppgThread,
        i2cBus2, P3_2, P3_3};
    status = ppgThread.start(callback(max30101_reader_task, &args_max30101));
    if (status != osOK) {
        printf("Starting thread_max30205_reader thread failed(%ld)!\r\n", status);
    }

    struct max30003_reader_task_args args_max30003 = {
            &ecgThread,
            spim2, SPI2_SS};
    status = ecgThread.start(callback(max30003_reader_task, &args_max30003));
    if (status != osOK) {
        printf("Starting thread_max30205_reader thread failed(%ld)!\r\n", status);
    }

    acc_msg_t acc_data;
    ppg_msg_t ppg_data;
    ecg_msg_t ecg_data;
    float ecg_hr;

    float acc_mean = 0;
    float acc_sigma = 0;

    float hr = 60;
    float spo2 = 100;
    float ecg_hr_mean = 60;

    while(1){
        uint32_t acc_buf_size, ppg_buf_size, ecg_buf_size, ecg_hr_buf_size;

        acc_buf_size = acc_buf.size();
        for(uint32_t i=0; i < acc_buf_size; i++){
            acc_buf.pop(acc_data);
            acc_mean = 0.9*acc_mean + 0.1*acc_data.acc_amp;
            acc_sigma = 0.95*acc_sigma + 0.05*powf(acc_data.acc_amp-acc_mean, 2);
            
        }

        ppg_buf_size = ppg_buf.size();
        for(uint32_t i=0; i < ppg_buf_size; i++){
            ppg_buf.pop(ppg_data);
            hr = 0.9*hr + 0.1*(float)ppg_data.hr;
            spo2 = 0.9*spo2 + 0.1*(float)ppg_data.spo2;
            //printf("HR_SPO2: %d,%d\n", ppg_data.hr, ppg_data.spo2);
        }

        ecg_buf_size = ecg_buf.size();
        for(uint32_t i=0; i < ecg_buf_size; i++){
            ecg_buf.pop(ecg_data);
            uint16_t nsamples = ecg_data.nsamples;
            for (uint16_t idx = 0; idx < nsamples; idx++){
                printf("ECG: %d\n", ecg_data.ecg_sample[idx]);    
            }
        }
        ecg_hr_buf_size = ecg_hr_buf.size();
        for(uint32_t i=0; i < ecg_hr_buf_size; i++){
            ecg_hr_buf.pop(ecg_hr);
            ecg_hr_mean = 0.9*ecg_hr_mean + 0.1*ecg_hr;
            //printf("ECGHR: %f\n", ecg_hr);
        }
        if(enSnsTick[ONESEC]){
            enSnsTick[ONESEC] = false;
            printf("DATA: %.1f, %.1f, %.1f, %.1f\n", acc_sigma*500, hr, ecg_hr_mean, spo2);
        }
    }
}


void SetStatusLed(uint8_t f)
{
    ledStatus |= f;
    
    UpdateStatusLed();
}


void ClearStatusLed(uint8_t f)
{
    ledStatus &= ~f;
    
    UpdateStatusLed();
}


void UpdateStatusLed()
{
    LedColors c;
    
    // Priority Encoder
    if (ledStatus & ST_ERROR)        c = RED;
    else if (ledStatus & ST_MSD)     c = BLUE;
    else if (ledStatus & ST_RECORD)  c = MAGENTA;
    else if (ledStatus & ST_USB_PWR) c = GREEN;
    else if (ledStatus & ST_GPS_FIX) c = CYAN;
    else if (ledStatus & ST_STARTUP) c = YELLOW;
    else                             c = BLACK;
    
    // Hardware control
    switch (c) {
        case BLACK:
            rLED = 1; gLED = 1; bLED = 1;
            break;
        case RED:
            rLED = 0; gLED = 1; bLED = 1;
            break;
        case YELLOW:
            rLED = 0; gLED = 0; bLED = 1;
            break;
        case GREEN:
            rLED = 1; gLED = 0; bLED = 1;
            break;
        case CYAN:
            rLED = 1; gLED = 0; bLED = 0;
            break;
        case BLUE:
            rLED = 1; gLED = 1; bLED = 0;
            break;
        case MAGENTA:
            rLED = 0; gLED = 1; bLED = 0;
            break;
        case WHITE:
            rLED = 0; gLED = 0; bLED = 0;
            break;
    }
}
