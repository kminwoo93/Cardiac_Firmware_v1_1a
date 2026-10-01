/*
 * cpu_usage.c
 *
 * Per-thread / per-ISR CPU usage measurement for ThreadX (see cpu_usage.h).
 *
 * Every hook charges the cycles elapsed since the previous hook to the
 * context that was running (a thread, an ISR, or idle) and then switches
 * to the new context. ISR time is exclusive: a nested ISR is not counted
 * in the ISR it preempted. Time spent in the scheduler with no thread
 * ready (PendSV + idle loop) is counted as idle.
 */

#include "cpu_usage.h"

#ifdef TX_ENABLE_EXECUTION_CHANGE_NOTIFY

#include "main.h"
#include "tx_api.h"
#include "tx_thread.h"

#define CPU_USAGE_WINDOW_HZ     1U      /* one result per second */
#define CPU_USAGE_ISR_DEPTH     8U      /* tracked ISR nesting depth */

/* Accumulator slots: threads, then idle, then ISRs */
#define CPU_USAGE_SLOT_IDLE     (CPU_USAGE_MAX_THREADS)
#define CPU_USAGE_SLOT_ISR(i)   (CPU_USAGE_MAX_THREADS + 1U + (i))
#define CPU_USAGE_SLOT_COUNT    (CPU_USAGE_MAX_THREADS + 1U + CPU_USAGE_MAX_ISRS)

#define CPU_USAGE_IRQN_SYSTICK  (-1)

volatile CPU_Usage_Thread cpu_usage_thread[CPU_USAGE_MAX_THREADS];
volatile CPU_Usage_Isr    cpu_usage_isr[CPU_USAGE_MAX_ISRS];
volatile uint32_t cpu_usage_thread_count = 0U;
volatile uint32_t cpu_usage_isr_count = 0U;
volatile uint32_t cpu_usage_total_x100 = 0U;
volatile uint32_t cpu_usage_idle_x100 = 0U;
volatile uint32_t cpu_usage_isr_total_x100 = 0U;
volatile uint32_t cpu_usage_total_peak_x100 = 0U;
volatile uint32_t cpu_usage_window_count = 0U;
volatile uint32_t cpu_usage_reset_peaks = 0U;

static TX_THREAD *cpu_usage_thread_ptr[CPU_USAGE_MAX_THREADS];
static uint32_t   cpu_usage_acc[CPU_USAGE_SLOT_COUNT];
static uint32_t   cpu_usage_mark;
static uint32_t   cpu_usage_window_start;
static uint32_t   cpu_usage_window_cycles;
static uint32_t   cpu_usage_thread_slot = CPU_USAGE_SLOT_IDLE;
static uint32_t   cpu_usage_isr_nest;
static uint8_t    cpu_usage_isr_stack[CPU_USAGE_ISR_DEPTH];
static uint8_t    cpu_usage_ready;

typedef struct
{
  int32_t     irqn;
  const char *name;
} CPU_Usage_IsrName;

static const CPU_Usage_IsrName cpu_usage_isr_names[] =
{
  { CPU_USAGE_IRQN_SYSTICK,        "SysTick (ThreadX tick)" },
  { (int32_t)EXTI0_IRQn,           "EXTI0 (ADS DRDY)" },
  { (int32_t)EXTI5_IRQn,           "EXTI5 (ICM INT)" },
  { (int32_t)GPDMA1_Channel0_IRQn, "GPDMA1_Ch0" },
  { (int32_t)TIM2_IRQn,            "TIM2" },
  { (int32_t)TIM6_IRQn,            "TIM6 (HAL tick)" },
  { (int32_t)USART2_IRQn,          "USART2 (BLE HCI)" },
  { (int32_t)OTG_HS_IRQn,          "OTG_HS (USB)" },
};

static uint32_t CPU_Usage_Lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void CPU_Usage_Unlock(uint32_t primask)
{
  __set_PRIMASK(primask);
}

static uint32_t CPU_Usage_CurrentSlot(void)
{
  uint32_t depth;

  if (cpu_usage_isr_nest == 0U)
  {
    return cpu_usage_thread_slot;
  }

  depth = (cpu_usage_isr_nest < CPU_USAGE_ISR_DEPTH) ? cpu_usage_isr_nest : CPU_USAGE_ISR_DEPTH;
  return cpu_usage_isr_stack[depth - 1U];
}

static uint32_t CPU_Usage_Percent(uint32_t cycles, uint32_t total)
{
  return (uint32_t)(((uint64_t)cycles * 10000U) / total);
}

static void CPU_Usage_UpdatePeak(volatile uint32_t *peak, uint32_t value)
{
  if (value > *peak)
  {
    *peak = value;
  }
}

/* Close the current window and publish the results */
static void CPU_Usage_Publish(uint32_t now)
{
  uint32_t total = now - cpu_usage_window_start;
  uint32_t isr_total = 0U;
  uint32_t value;
  uint32_t i;

  if (cpu_usage_reset_peaks != 0U)
  {
    for (i = 0U; i < CPU_USAGE_MAX_THREADS; i++)
    {
      cpu_usage_thread[i].peak_x100 = 0U;
    }
    for (i = 0U; i < CPU_USAGE_MAX_ISRS; i++)
    {
      cpu_usage_isr[i].peak_x100 = 0U;
    }
    cpu_usage_total_peak_x100 = 0U;
    cpu_usage_reset_peaks = 0U;
  }

  for (i = 0U; i < cpu_usage_thread_count; i++)
  {
    value = CPU_Usage_Percent(cpu_usage_acc[i], total);
    cpu_usage_thread[i].usage_x100 = value;
    CPU_Usage_UpdatePeak(&cpu_usage_thread[i].peak_x100, value);
  }

  for (i = 0U; i < cpu_usage_isr_count; i++)
  {
    isr_total += cpu_usage_acc[CPU_USAGE_SLOT_ISR(i)];
    value = CPU_Usage_Percent(cpu_usage_acc[CPU_USAGE_SLOT_ISR(i)], total);
    cpu_usage_isr[i].usage_x100 = value;
    CPU_Usage_UpdatePeak(&cpu_usage_isr[i].peak_x100, value);
  }

  cpu_usage_isr_total_x100 = CPU_Usage_Percent(isr_total, total);
  cpu_usage_idle_x100 = CPU_Usage_Percent(cpu_usage_acc[CPU_USAGE_SLOT_IDLE], total);
  cpu_usage_total_x100 = (cpu_usage_idle_x100 < 10000U) ? (10000U - cpu_usage_idle_x100) : 0U;
  CPU_Usage_UpdatePeak(&cpu_usage_total_peak_x100, cpu_usage_total_x100);

  for (i = 0U; i < CPU_USAGE_SLOT_COUNT; i++)
  {
    cpu_usage_acc[i] = 0U;
  }

  cpu_usage_window_start = now;
  cpu_usage_window_count++;
}

/* Charge the cycles elapsed since the last hook to the context that was
 * running, and close the window once a second has passed.
 */
static void CPU_Usage_Charge(void)
{
  uint32_t now = DWT->CYCCNT;

  cpu_usage_acc[CPU_Usage_CurrentSlot()] += now - cpu_usage_mark;
  cpu_usage_mark = now;

  if ((now - cpu_usage_window_start) >= cpu_usage_window_cycles)
  {
    CPU_Usage_Publish(now);
  }
}

static uint32_t CPU_Usage_ThreadSlot(TX_THREAD *thread)
{
  uint32_t i;

  if (thread == TX_NULL)
  {
    return CPU_USAGE_SLOT_IDLE;
  }

  for (i = 0U; i < cpu_usage_thread_count; i++)
  {
    if (cpu_usage_thread_ptr[i] == thread)
    {
      return i;
    }
  }

  if (cpu_usage_thread_count >= CPU_USAGE_MAX_THREADS)
  {
    /* Table full: the extra thread is counted as idle */
    return CPU_USAGE_SLOT_IDLE;
  }

  i = cpu_usage_thread_count;
  cpu_usage_thread_ptr[i] = thread;
  cpu_usage_thread[i].name = thread->tx_thread_name;
  cpu_usage_thread[i].usage_x100 = 0U;
  cpu_usage_thread[i].peak_x100 = 0U;
  cpu_usage_thread_count = i + 1U;
  return i;
}

static uint32_t CPU_Usage_IsrSlot(void)
{
  int32_t irqn = (int32_t)(__get_IPSR() & 0x1FFU) - 16;
  uint32_t i;

  for (i = 0U; i < cpu_usage_isr_count; i++)
  {
    if (cpu_usage_isr[i].irqn == irqn)
    {
      return CPU_USAGE_SLOT_ISR(i);
    }
  }

  if (cpu_usage_isr_count >= CPU_USAGE_MAX_ISRS)
  {
    /* Table full: charge it to the last entry */
    return CPU_USAGE_SLOT_ISR(CPU_USAGE_MAX_ISRS - 1U);
  }

  i = cpu_usage_isr_count;
  cpu_usage_isr[i].irqn = irqn;
  cpu_usage_isr[i].name = "IRQ";
  for (uint32_t n = 0U; n < (sizeof(cpu_usage_isr_names) / sizeof(cpu_usage_isr_names[0])); n++)
  {
    if (cpu_usage_isr_names[n].irqn == irqn)
    {
      cpu_usage_isr[i].name = cpu_usage_isr_names[n].name;
      break;
    }
  }
  cpu_usage_isr[i].usage_x100 = 0U;
  cpu_usage_isr[i].peak_x100 = 0U;
  cpu_usage_isr_count = i + 1U;
  return CPU_USAGE_SLOT_ISR(i);
}

/* Called by tx_kernel_enter() before the first thread is scheduled */
void _tx_execution_initialize(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  cpu_usage_window_cycles = SystemCoreClock / CPU_USAGE_WINDOW_HZ;
  cpu_usage_mark = DWT->CYCCNT;
  cpu_usage_window_start = cpu_usage_mark;
  cpu_usage_thread_slot = CPU_USAGE_SLOT_IDLE;
  cpu_usage_isr_nest = 0U;
  cpu_usage_ready = 1U;
}

/* Called from PendSV when a thread starts running (_tx_thread_current_ptr
 * already points to it).
 */
void _tx_execution_thread_enter(void)
{
  uint32_t primask;

  if (cpu_usage_ready == 0U)
  {
    return;
  }

  primask = CPU_Usage_Lock();
  CPU_Usage_Charge();
  cpu_usage_thread_slot = CPU_Usage_ThreadSlot(_tx_thread_current_ptr);
  CPU_Usage_Unlock(primask);
}

/* Called from PendSV when the running thread stops running */
void _tx_execution_thread_exit(void)
{
  uint32_t primask;

  if (cpu_usage_ready == 0U)
  {
    return;
  }

  primask = CPU_Usage_Lock();
  CPU_Usage_Charge();
  cpu_usage_thread_slot = CPU_USAGE_SLOT_IDLE;
  CPU_Usage_Unlock(primask);
}

void _tx_execution_isr_enter(void)
{
  uint32_t primask;

  if (cpu_usage_ready == 0U)
  {
    return;
  }

  primask = CPU_Usage_Lock();
  CPU_Usage_Charge();
  if (cpu_usage_isr_nest < CPU_USAGE_ISR_DEPTH)
  {
    cpu_usage_isr_stack[cpu_usage_isr_nest] = (uint8_t)CPU_Usage_IsrSlot();
  }
  cpu_usage_isr_nest++;
  CPU_Usage_Unlock(primask);
}

void _tx_execution_isr_exit(void)
{
  uint32_t primask;

  if (cpu_usage_ready == 0U)
  {
    return;
  }

  primask = CPU_Usage_Lock();
  CPU_Usage_Charge();
  if (cpu_usage_isr_nest > 0U)
  {
    cpu_usage_isr_nest--;
  }
  CPU_Usage_Unlock(primask);
}

#endif /* TX_ENABLE_EXECUTION_CHANGE_NOTIFY */
