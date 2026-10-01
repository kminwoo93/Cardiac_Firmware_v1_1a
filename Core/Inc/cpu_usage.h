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

#endif /* TX_ENABLE_EXECUTION_CHANGE_NOTIFY */

#ifdef __cplusplus
}
#endif

#endif /* INC_CPU_USAGE_H_ */
