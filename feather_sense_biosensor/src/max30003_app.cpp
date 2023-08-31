/*
 * max30003_app.c
 *
 *  Created on: Jun 20, 2018
 *      Author: Mahir.Ozturk
 */
#include <mbed.h>
#include <max30003_app.h>
#include <MAX30003.h>
#include <data.h>

#define MAX30003_IRQ_ASSERTED_SIGNAL_ID	1

static Thread *thread = 0;
EventFlags max30003_irq_flags;

CircularBuffer<ecg_msg_t, MAX_BUF_SIZE> ecg_buf;
CircularBuffer<float, MAX_BUF_SIZE> ecg_hr_buf;

/* ECG FIFO nearly full callback */
void ecgFIFO_callback()  {
	if (thread != 0) {
		//thread->signal_set(MAX30003_IRQ_ASSERTED_SIGNAL_ID);
		max30003_irq_flags.set(MAX30003_IRQ_ASSERTED_SIGNAL_ID);
	}
}

void ecg_config(MAX30003& ecgAFE)
{
	// Reset ECG to clear registers
	ecgAFE.writeRegister( MAX30003::SW_RST , 0);

	// General config register setting
	MAX30003::GeneralConfiguration_u CNFG_GEN_r;
	CNFG_GEN_r.bits.en_ecg = 1;     // Enable ECG channel
	CNFG_GEN_r.bits.rbiasn = 1;     // Enable resistive bias on negative input
	CNFG_GEN_r.bits.rbiasp = 1;     // Enable resistive bias on positive input
	CNFG_GEN_r.bits.en_rbias = 1;   // Enable resistive bias
	CNFG_GEN_r.bits.imag = 2;       // Current magnitude = 10nA
	CNFG_GEN_r.bits.en_dcloff = 1;  // Enable DC lead-off detection
	ecgAFE.writeRegister( MAX30003::CNFG_GEN , CNFG_GEN_r.all);


	// ECG Config register setting
	MAX30003::ECGConfiguration_u CNFG_ECG_r;
	CNFG_ECG_r.bits.dlpf = 1;       // Digital LPF cutoff = 40Hz
	CNFG_ECG_r.bits.dhpf = 1;       // Digital HPF cutoff = 0.5Hz
	CNFG_ECG_r.bits.gain = 3;       // ECG gain = 160V/V
	CNFG_ECG_r.bits.rate = 2;       // Sample rate = 128 sps
	ecgAFE.writeRegister( MAX30003::CNFG_ECG , CNFG_ECG_r.all);


	//R-to-R configuration
	MAX30003::RtoR1Configuration_u CNFG_RTOR_r;
	CNFG_RTOR_r.bits.wndw = 0b0011;         // WNDW = 96ms
	CNFG_RTOR_r.bits.rgain = 0b1111;        // Auto-scale gain
	CNFG_RTOR_r.bits.pavg = 0b11;           // 16-average
	CNFG_RTOR_r.bits.ptsf = 0b0011;         // PTSF = 4/16
	CNFG_RTOR_r.bits.en_rtor = 1;           // Enable R-to-R detection
	ecgAFE.writeRegister( MAX30003::CNFG_RTOR1 , CNFG_RTOR_r.all);


	//Manage interrupts register setting
	MAX30003::ManageInterrupts_u MNG_INT_r;
	MNG_INT_r.bits.efit = 0b00011;          // Assert EINT w/ 4 unread samples
	MNG_INT_r.bits.clr_rrint = 0b01;        // Clear R-to-R on RTOR reg. read back
	ecgAFE.writeRegister( MAX30003::MNGR_INT , MNG_INT_r.all);


	//Enable interrupts register setting
	MAX30003::EnableInterrupts_u EN_INT_r;
	EN_INT_r.bits.en_eint = 1;              // Enable EINT interrupt
	EN_INT_r.bits.en_rrint = 1;             // Enable R-to-R interrupt
	EN_INT_r.bits.intb_type = 3;            // Open-drain NMOS with internal pullup
	ecgAFE.writeRegister( MAX30003::EN_INT , EN_INT_r.all);


	//Dyanmic modes config
	MAX30003::ManageDynamicModes_u MNG_DYN_r;
	MNG_DYN_r.bits.fast = 0;                // Fast recovery mode disabled
	ecgAFE.writeRegister( MAX30003::MNGR_DYN , MNG_DYN_r.all);

	// MUX Config
	MAX30003::MuxConfiguration_u CNFG_MUX_r;
	CNFG_MUX_r.bits.openn = 0;          // Connect ECGN to AFE channel
	CNFG_MUX_r.bits.openp = 0;          // Connect ECGP to AFE channel
	ecgAFE.writeRegister( MAX30003::CNFG_EMUX , CNFG_MUX_r.all);

	return;
}

void max30003_reader_task(struct max30003_reader_task_args *args)
{
	// Constants
	const int EINT_STATUS = 1 << 23;
	const int RTOR_STATUS = 1 << 10;
	const int RTOR_REG_OFFSET = 10;
	const float RTOR_LSB_RES = 0.0078125f;
	const int FIFO_OVF = 0x7;
	const int FIFO_VALID_SAMPLE = 0x0;
	const int FIFO_FAST_SAMPLE = 0x1;
	const int ETAG_BITS = 0x7;

	uint32_t ecgFIFO, RtoR, readECGSamples, ETAG[32], status, idx;
	float BPM;
	int16_t ecgSample[32];

	thread = args->self;

	MAX30003 max30003(args->spiBus, args->spiCS);		/* MAX30003WING board */

	InterruptIn ecgFIFO_int(P5_4);          // Config P5_4 as int. in for the
	ecgFIFO_int.fall(&ecgFIFO_callback);    // ecg FIFO almost full interrupt

	ecg_config(max30003);                   // Config ECG

	max30003.writeRegister( MAX30003::SYNCH , 0);

	ecg_msg_t ecg_data;

	while (1) {
		// Read back ECG samples from the FIFO
		//thread->signal_wait(MAX30003_IRQ_ASSERTED_SIGNAL_ID);
		max30003_irq_flags.wait_all(MAX30003_IRQ_ASSERTED_SIGNAL_ID);

		while (1) {
			/* Read back ECG samples from the FIFO */
			status = max30003.readRegister( MAX30003::STATUS );      // Read the STATUS register

			if ((status & (RTOR_STATUS | EINT_STATUS)) == 0) {
				break;
			}

			// Check if R-to-R interrupt asserted
			if ((status & RTOR_STATUS) == RTOR_STATUS) {

				// printf("R-to-R Interrupt \r\n");

				// Read RtoR register
				RtoR = max30003.readRegister(MAX30003::RTOR) >>  RTOR_REG_OFFSET;

				// Convert to BPM
				BPM = 1.0f / ( RtoR * RTOR_LSB_RES / 60.0f );
				ecg_hr_buf.push(BPM);
				
			}

			// Check if EINT interrupt asserted
			if ((status & EINT_STATUS) == EINT_STATUS) {
				readECGSamples = 0;	// Reset sample counter

				do {
					ecgFIFO = max30003.readRegister( MAX30003::ECG_FIFO );	// Read FIFO
					ETAG[readECGSamples] = ( ecgFIFO >> 3 ) & ETAG_BITS;	// Isolate ETAG
					ecgSample[readECGSamples] = ecgFIFO >> 8;
					//printf("ECG: %d\r\n", ETAG[readECGSamples]);
					
					readECGSamples++;										// Increment sample counter
					
				// Check that sample is not last sample in FIFO
				} while (ETAG[readECGSamples-1] == FIFO_VALID_SAMPLE ||
						ETAG[readECGSamples-1] == FIFO_FAST_SAMPLE);

				// Check if FIFO has overflowed
				if (ETAG[readECGSamples - 1] == FIFO_OVF){
					max30003.writeRegister( MAX30003::FIFO_RST , 0); // Reset FIFO
				}

				ecg_data.nsamples = readECGSamples;
				for( idx = 0; idx < readECGSamples; idx++ ) {
					ecg_data.ecg_sample[idx] = ecgSample[idx];
                } 
				ecg_buf.push(ecg_data);
			}
		}
    }
}


