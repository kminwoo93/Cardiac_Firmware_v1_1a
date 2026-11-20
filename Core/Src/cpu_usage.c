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

volatile CPU_Prof_Section cpu_prof[CPU_PROF_COUNT];
volatile uint32_t cpu_prof_total_x100 = 0U;

#define CPU_PROF_NONE           0xFFU

static const char *const cpu_prof_names[CPU_PROF_COUNT] =
{
  [CPU_PROF_ECG_READ]       = "ECG read (SPI)",
  [CPU_PROF_ECG_FILTER]     = "ECG CH2 filter",
  [CPU_PROF_ECG_DETECT]     = "ECG R-peak detect",
  [CPU_PROF_IMU_READ]       = "IMU read (SPI 12 B)",
  [CPU_PROF_TEMP_MEASURE]   = "Temp measure (I2C)",
  [CPU_PROF_USB_CSV_ECG]    = "USB CSV E row",
  [CPU_PROF_USB_CSV_SCG]    = "USB CSV S row",
  [CPU_PROF_USB_CSV_RPEAK]  = "USB CSV R row",
  [CPU_PROF_USB_CSV_TEMP]   = "USB CSV T row",
  [CPU_PROF_USB_WRITE]      = "USB CDC write",
  [CPU_PROF_BLE_HCI_EVENTS] = "BLE HCI events",
  [CPU_PROF_BLE_LINK]       = "BLE link setup",
  [CPU_PROF_BLE_STATUS]     = "BLE Status",
  [CPU_PROF_BLE_RPEAK]      = "BLE R-peak",
  [CPU_PROF_BLE_TEMP]       = "BLE Temperature",
  [CPU_PROF_BLE_ECG]        = "BLE ECG stream",
  [CPU_PROF_BLE_SCG]        = "BLE SCG stream",
};

/* Section open in each thread slot (CPU_PROF_NONE = none) */
static uint8_t  cpu_prof_active[CPU_USAGE_MAX_THREADS];
static uint32_t cpu_prof_acc[CPU_PROF_COUNT];        /* cycles, this window */
static uint32_t cpu_prof_calls[CPU_PROF_COUNT];      /* calls, this window */
static uint32_t cpu_prof_call_cycles[CPU_PROF_COUNT];/* cycles, open call */
static uint32_t cpu_prof_call_max[CPU_PROF_COUNT];   /* cycles, this window */

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
  { (int32_t)I2C1_EV_IRQn,         "I2C1_EV (MAX30205)" },
  { (int32_t)I2C1_ER_IRQn,         "I2C1_ER (MAX30205)" },
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

static uint32_t CPU_Prof_CyclesToNs(uint64_t cycles)
{
  uint64_t ns = (cycles * 1000000000ULL) / SystemCoreClock;

  return (ns > UINT32_MAX) ? UINT32_MAX : (uint32_t)ns;
}

/* Publish the section results of the window that just closed */
static void CPU_Prof_Publish(uint32_t total, uint32_t reset_peaks)
{
  uint32_t sum = 0U;
  uint32_t value;
  uint32_t i;

  for (i = 0U; i < CPU_PROF_COUNT; i++)
  {
    if (reset_peaks != 0U)
    {
      cpu_prof[i].peak_x100 = 0U;
      cpu_prof[i].peak_max_ns = 0U;
    }

    sum += cpu_prof_acc[i];
    value = CPU_Usage_Percent(cpu_prof_acc[i], total);
    cpu_prof[i].usage_x100 = value;
    CPU_Usage_UpdatePeak(&cpu_prof[i].peak_x100, value);

    cpu_prof[i].calls_per_s = cpu_prof_calls[i];
    cpu_prof[i].avg_ns = (cpu_prof_calls[i] == 0U) ? 0U :
        CPU_Prof_CyclesToNs(cpu_prof_acc[i] / cpu_prof_calls[i]);
    value = CPU_Prof_CyclesToNs(cpu_prof_call_max[i]);
    cpu_prof[i].max_ns = value;
    CPU_Usage_UpdatePeak(&cpu_prof[i].peak_max_ns, value);

    cpu_prof_acc[i] = 0U;
    cpu_prof_calls[i] = 0U;
    cpu_prof_call_max[i] = 0U;
  }

  cpu_prof_total_x100 = CPU_Usage_Percent(sum, total);
}

/* Close the current window and publish the results */
static void CPU_Usage_Publish(uint32_t now)
{
  uint32_t total = now - cpu_usage_window_start;
  uint32_t isr_total = 0U;
  uint32_t value;
  uint32_t i;

  CPU_Prof_Publish(total, cpu_usage_reset_peaks);

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
  uint32_t delta = now - cpu_usage_mark;
  uint32_t slot = CPU_Usage_CurrentSlot();

  cpu_usage_acc[slot] += delta;
  cpu_usage_mark = now;

  /* A thread (not an ISR) ran: also charge its open section, if any */
  if ((cpu_usage_isr_nest == 0U) && (slot < CPU_USAGE_MAX_THREADS) &&
      (cpu_prof_active[slot] != CPU_PROF_NONE))
  {
    cpu_prof_acc[cpu_prof_active[slot]] += delta;
    cpu_prof_call_cycles[cpu_prof_active[slot]] += delta;
  }

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

  for (uint32_t i = 0U; i < CPU_USAGE_MAX_THREADS; i++)
  {
    cpu_prof_active[i] = CPU_PROF_NONE;
  }
  for (uint32_t i = 0U; i < CPU_PROF_COUNT; i++)
  {
    cpu_prof[i].name = cpu_prof_names[i];
  }

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

/* Close the section open in this thread slot and count the call */
static void CPU_Prof_Close(uint32_t slot)
{
  uint32_t id = cpu_prof_active[slot];

  if (id == CPU_PROF_NONE)
  {
    return;
  }

  cpu_prof_calls[id]++;
  if (cpu_prof_call_cycles[id] > cpu_prof_call_max[id])
  {
    cpu_prof_call_max[id] = cpu_prof_call_cycles[id];
  }
  cpu_prof_active[slot] = CPU_PROF_NONE;
}

void CPU_Prof_Begin(CPU_Prof_Id id)
{
  uint32_t primask;
  uint32_t slot;

  if ((cpu_usage_ready == 0U) || ((uint32_t)id >= CPU_PROF_COUNT))
  {
    return;
  }

  primask = CPU_Usage_Lock();
  slot = cpu_usage_thread_slot;
  if ((cpu_usage_isr_nest == 0U) && (slot < CPU_USAGE_MAX_THREADS))
  {
    CPU_Usage_Charge();
    CPU_Prof_Close(slot);
    cpu_prof_active[slot] = (uint8_t)id;
    cpu_prof_call_cycles[id] = 0U;
  }
  CPU_Usage_Unlock(primask);
}

void CPU_Prof_End(CPU_Prof_Id id)
{
  uint32_t primask;
  uint32_t slot;

  if ((cpu_usage_ready == 0U) || ((uint32_t)id >= CPU_PROF_COUNT))
  {
    return;
  }

  primask = CPU_Usage_Lock();
  slot = cpu_usage_thread_slot;
  if ((cpu_usage_isr_nest == 0U) && (slot < CPU_USAGE_MAX_THREADS) &&
      (cpu_prof_active[slot] == (uint8_t)id))
  {
    CPU_Usage_Charge();
    CPU_Prof_Close(slot);
  }
  CPU_Usage_Unlock(primask);
}

#endif /* TX_ENABLE_EXECUTION_CHANGE_NOTIFY */
