/*
 * cpu_usage.h
 *
 * Per-thread / per-ISR CPU usage measurement for ThreadX.
 *
 * Uses the ThreadX execution change hooks (_tx_execution_thread_enter/exit,
 * _tx_execution_isr_enter/exit) and the DWT cycle counter. Every second the
 * cycles spent in each thread, each ISR and idle are converted to a
 * percentage (x100, 1234 = 12.34 %) and published in the variables below,
 * which can be watched in the STM32CubeIDE Live Expressions view.
 *
 * Enabled when TX_ENABLE_EXECUTION_CHANGE_NOTIFY is defined for both the C
 * compiler and the assembler (Project Properties > C/C++ Build > Settings >
 * MCU GCC Assembler / MCU GCC Compiler > Preprocessor). Without it the
 * module compiles to nothing and the macros below are empty.
 */

#ifndef INC_CPU_USAGE_H_
#define INC_CPU_USAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CPU_USAGE_MAX_THREADS   16U
#define CPU_USAGE_MAX_ISRS      12U

typedef struct
{
  const char *name;       /* ThreadX thread name */
  uint32_t    usage_x100; /* last 1 s window, 0.01 % units */
  uint32_t    peak_x100;  /* highest window since boot / last peak reset */
} CPU_Usage_Thread;

typedef struct
{
  const char *name;       /* handler name (or "IRQ" when not in the table) */
  int32_t     irqn;       /* IRQn_Type value, -1 = SysTick */
  uint32_t    usage_x100;
  uint32_t    peak_x100;
} CPU_Usage_Isr;

/*
 * Code sections measured inside the threads (docs/CPU_PROFILING.md).
 *
 * A section is wrapped with CPU_PROF_BEGIN(id) / CPU_PROF_END(id) in thread
 * code. It is charged only the cycles its thread actually runs between the
 * two calls: time the thread is blocked (queue wait, sleep, I2C/USB wait on
 * a semaphore), preempted by another thread, or interrupted by an ISR is
 * not counted. The thread total in cpu_usage_thread[] still includes it.
 *
 * One section per thread at a time: CPU_PROF_BEGIN() closes a section the
 * same thread left open (for example after a "break" that skipped the END).
 */
typedef enum
{
  CPU_PROF_ECG_READ = 0,   /* ADS1292R SPI frame read             (ECG acq)  */
  CPU_PROF_ECG_FILTER,     /* CH2 IIR filter chain                (ECG acq)  */
  CPU_PROF_ECG_DETECT,     /* Pan-Tompkins R-peak detector        (ECG proc) */
  CPU_PROF_IMU_READ,       /* ICM-20948 12-byte SPI burst         (SCG acq)  */
  CPU_PROF_TEMP_MEASURE,   /* MAX30205 one-shot + I2C read        (temp)     */
  CPU_PROF_USB_CSV_ECG,    /* format one E row                    (USB)      */
  CPU_PROF_USB_CSV_SCG,    /* format one S row                    (USB)      */
  CPU_PROF_USB_CSV_RPEAK,  /* format one R row                    (USB)      */
  CPU_PROF_USB_CSV_TEMP,   /* format one T row                    (USB)      */
  CPU_PROF_USB_WRITE,      /* ux_device_class_cdc_acm_write()     (USB)      */
  CPU_PROF_BLE_HCI_EVENTS, /* hci_user_evt_proc()                 (BLE)      */
  CPU_PROF_BLE_LINK,       /* link setup (MTU, PHY, DLE, params)  (BLE)      */
  CPU_PROF_BLE_STATUS,     /* Status characteristic, 1 s          (BLE)      */
  CPU_PROF_BLE_RPEAK,      /* R-peak notifications                (BLE)      */
  CPU_PROF_BLE_TEMP,       /* Temperature notifications           (BLE)      */
  CPU_PROF_BLE_ECG,        /* ECG stream packets                  (BLE)      */
  CPU_PROF_BLE_SCG,        /* SCG stream packets                  (BLE)      */
  CPU_PROF_COUNT
} CPU_Prof_Id;

typedef struct
{
  const char *name;
  uint32_t    usage_x100;    /* CPU share in the last 1 s window, 0.01 % */
  uint32_t    peak_x100;     /* highest usage_x100 since boot / reset */
  uint32_t    calls_per_s;   /* completed calls in the last window */
  uint32_t    avg_ns;        /* CPU time per call, last window */
  uint32_t    max_ns;        /* longest call in the last window */
  uint32_t    peak_max_ns;   /* longest call since boot / reset */
} CPU_Prof_Section;

#ifdef TX_ENABLE_EXECUTION_CHANGE_NOTIFY

/* Results (updated once per second) */
extern volatile CPU_Usage_Thread cpu_usage_thread[CPU_USAGE_MAX_THREADS];
extern volatile CPU_Usage_Isr    cpu_usage_isr[CPU_USAGE_MAX_ISRS];
extern volatile uint32_t cpu_usage_thread_count;
extern volatile uint32_t cpu_usage_isr_count;
extern volatile uint32_t cpu_usage_total_x100;     /* 100 % - idle */
extern volatile uint32_t cpu_usage_idle_x100;
extern volatile uint32_t cpu_usage_isr_total_x100; /* sum of all ISRs */
extern volatile uint32_t cpu_usage_total_peak_x100;
extern volatile uint32_t cpu_usage_window_count;   /* increments every window */
extern volatile uint32_t cpu_usage_reset_peaks;    /* write 1 to clear peaks */

/* Section results (updated once per second, cleared by cpu_usage_reset_peaks) */
extern volatile CPU_Prof_Section cpu_prof[CPU_PROF_COUNT];
extern volatile uint32_t cpu_prof_total_x100;      /* sum of all sections */

/* Mark a section in thread code. Ignored in ISRs and before the kernel runs. */
void CPU_Prof_Begin(CPU_Prof_Id id);
void CPU_Prof_End(CPU_Prof_Id id);

#define CPU_PROF_BEGIN(id)      CPU_Prof_Begin(id)
#define CPU_PROF_END(id)        CPU_Prof_End(id)

/* ThreadX execution change hooks (called from the port / ISRs) */
void _tx_execution_initialize(void);
void _tx_execution_thread_enter(void);
void _tx_execution_thread_exit(void);
void _tx_execution_isr_enter(void);
void _tx_execution_isr_exit(void);

/* Put these at the start / end of every peripheral IRQ handler so that ISR
 * time is not charged to the interrupted thread. SysTick is already covered
 * by tx_initialize_low_level.S.
 */
#define CPU_USAGE_ISR_ENTER()   _tx_execution_isr_enter()
#define CPU_USAGE_ISR_EXIT()    _tx_execution_isr_exit()

#else

#define CPU_USAGE_ISR_ENTER()
#define CPU_USAGE_ISR_EXIT()

#define CPU_PROF_BEGIN(id)
#define CPU_PROF_END(id)

#endif /* TX_ENABLE_EXECUTION_CHANGE_NOTIFY */

#ifdef __cplusplus
}
#endif

#endif /* INC_CPU_USAGE_H_ */
