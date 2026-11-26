/*
 * FreeRTOSConfig.h  --  shared by all Event-Driven-Gates samples
 *
 * ONE config, TWO targets (pick with -DEDG_TARGET=sim|hw, see ../BUILDING.md):
 *
 *   sim   RP2040, Raspberry Pi Pico       -- port portable/ThirdParty/GCC/RP2040
 *         The Wokwi simulator only emulates the RP2040, so this is the build
 *         every diagram.json + wokwi.toml in this tree runs.
 *
 *   hw    RP2350, Raspberry Pi Pico 2 W   -- port .../GCC/RP2350_ARM_NTZ
 *         Arm Cortex-M33 cores. This is the build you flash.
 *
 * Everything below is shared. The few genuinely chip-specific options are
 * bracketed by PICO_RP2350, which the SDK defines for RP2350 platforms only.
 *
 * We run the kernel on a SINGLE core (configNUMBER_OF_CORES = 1) on both. That
 * is the simplest configuration and is plenty for the event-driven gate
 * networks here, whose objects spend nearly all their time blocked on a queue.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* PICO_RP2350 arrives as a -D on the compiler command line (for C *and* for the
 * assembler), so it is usable here without including any SDK header -- which
 * matters, because the ports include this file from portasm.S too. */

/*-----------------------------------------------------------
 * RP2350 (Armv8-M) mandatory port options.
 *
 * These four are REQUIRED by the RP2350 Arm port (see its README): the kernel
 * runs at a single privilege level in the secure state, MPU and TrustZone off.
 * The RP2040 is a Cortex-M0+ -- it has no MPU option, no TrustZone and no FPU,
 * and its port ignores these entirely.
 *----------------------------------------------------------*/
#if PICO_RP2350 && !defined(__riscv)
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1
#define configENABLE_FPU                        1
#endif

/*-----------------------------------------------------------
 * Exception handler installation -- STATIC, not dynamic. (RP2040 and RP2350
 * both honour this; both ports default it to 1.)
 *
 * Defaulted to 1, the port installs SVCall, PendSV
 * and SysTick at run time with exception_set_exclusive_handler(). Those writes
 * land in the SDK's *RAM* vector table, which is only reached because the SDK
 * retargets VTOR during startup. The FLASH vector table keeps pointing at the
 * SDK's weak default handlers -- and those are infinite loops.
 *
 * So on anything that does not honour the VTOR relocation (some simulators, a
 * debugger that resets the core, an unusual boot path) the `svc 0` inside
 * vTaskStartScheduler() jumps into a dead loop: the startup banner prints from
 * main() and then not a single task, tick or LED ever runs.
 *
 * Setting this to 0 makes portmacro.h alias SVC_Handler / PendSV_Handler /
 * SysTick_Handler onto the SDK's weak isr_svcall / isr_pendsv / isr_systick, so
 * the real handlers are linked straight into the flash vector table. The kernel
 * then works whether or not VTOR is ever relocated. We never re-hook exceptions
 * at run time, so nothing is lost.
 *
 * (The equivalent SDK-side switch, PICO_NO_RAM_VECTOR_TABLE=1, does not compile
 * against pico-sdk 2.x: hardware_exception/exception.c returns `current` from
 * outside the #if that declares it.)
 *
 * The RISC-V port is unaffected -- it has no SVCall/PendSV and drives the tick
 * from the SIO mtimecmp IRQ -- so leave it at the port's own default there.
 *----------------------------------------------------------*/
#ifndef __riscv
#define configUSE_DYNAMIC_EXCEPTION_HANDLERS    0
#endif

/*-----------------------------------------------------------
 * SMP / core configuration -- run on one core.
 *----------------------------------------------------------*/
#define configNUMBER_OF_CORES                   1
#define configTICK_CORE                         0
#define configUSE_CORE_AFFINITY                 0
#define configRUN_MULTIPLE_PRIORITIES           1

/*-----------------------------------------------------------
 * SDK <-> FreeRTOS interop -- OFF. This is the fix for the Wokwi freeze.
 *
 * The Raspberry Pi RP2xxx FreeRTOS ports default both of these to 1. They make
 * the SDK's own lock/sleep primitives cooperate with the scheduler, by replacing
 * the SDK's wait/notify with versions built on the `wfe` / `sev` instruction
 * pair (and, for cross-core, a FIFO IRQ and spin locks).
 *
 * rp2040js -- the RP2040 emulator behind Wokwi -- implements `sev` as a NO-OP.
 * So a wait that relies on a matching `sev` to wake it cannot make progress, and
 * with this interop enabled the scheduler wedges the moment it starts: the
 * simulator freezes at 0 % of real time (interop off = 100 % of real time).
 *
 * Turning it off is exactly what the known-good Wokwi FreeRTOS project does -- it
 * uses the generic ARM_CM0 port, which has no such interop. For these samples it
 * costs nothing: objects communicate through FreeRTOS queues, never through SDK
 * sync objects, and every task runs at one priority with time slicing, so the
 * one SDK lock in play (stdio's, behind printf) cannot cause priority inversion.
 *
 * Off on BOTH targets, to keep the sim and hw builds behaving identically. Must
 * be set before the port's rp2040_config.h, which #ifndef-guards both -- this
 * header is included first, so these win. */
#ifndef configSUPPORT_PICO_SYNC_INTEROP
#define configSUPPORT_PICO_SYNC_INTEROP         0
#endif
#ifndef configSUPPORT_PICO_TIME_INTEROP
#define configSUPPORT_PICO_TIME_INTEROP         0
#endif

/*-----------------------------------------------------------
 * Scheduler behaviour
 *----------------------------------------------------------*/
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

/* Tickless idle. When no task is ready the port stops SysTick, sleeps on `wfi`
 * until the next task is due, then credits the kernel with the ticks that
 * elapsed (vPortSuppressTicksAndSleep, in both the RP2040 and RP2350 ports).
 *
 * In an event-driven gate network every object is blocked on a queue nearly all
 * of the time, so this is nearly all of the time. Without it FreeRTOS's idle
 * task is a busy loop -- free on silicon, ruinous under emulation, where Wokwi
 * must interpret every instruction the core retires while a HALTED core costs it
 * nothing.
 *
 * This is what the known-good Wokwi FreeRTOS project does (Tony Smith's RP2040
 * template, wokwi.com/projects/413080804206920705), and it runs at 100 % of real
 * time. Note its idle path is woken by SysTick alone -- so `wfi` does wake on the
 * Cortex core timer under Wokwi.
 *
 * Caveat: ticks are skipped, not counted, while asleep. vTaskStepTick() credits
 * them afterwards, so vTaskDelay() stays accurate. */
#ifndef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE                 1
#endif
/* Informational only -- the SDK sets the real clock, and both ports program the
 * tick from clock_get_hz(clk_sys) at run time. RP2350 boots at 150 MHz, the
 * RP2040 at 125 MHz. */
#if PICO_RP2350
#define configCPU_CLOCK_HZ                      150000000
#else
#define configCPU_CLOCK_HZ                      125000000
#endif
#define configTICK_RATE_HZ                      10
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                (configSTACK_DEPTH_TYPE)256
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_APPLICATION_TASK_TAG          0
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/*-----------------------------------------------------------
 * Memory allocation
 *----------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
/* The RP2350 has 520 KB of SRAM, the RP2040 only 264 KB. The largest sample
 * (11-alu-1bit) needs roughly 40 KB of task stacks and queues. */
#if PICO_RP2350
#define configTOTAL_HEAP_SIZE                   (128 * 1024)
#else
#define configTOTAL_HEAP_SIZE                   (96 * 1024)
#endif
#define configAPPLICATION_ALLOCATED_HEAP        0

/*-----------------------------------------------------------
 * Hooks
 *----------------------------------------------------------*/
/* No idle or tick hook: configUSE_TICKLESS_IDLE above already halts the core,
 * and it does so better -- it stops the tick as well, rather than waking on every
 * tick. An idle hook that also slept would run BEFORE the tickless sleep and
 * defeat it. (The known-good project referenced above likewise has no idle hook.) */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

/* Both hooks below are implemented in edg.c and turn a silent hang into a message. */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/*-----------------------------------------------------------
 * Run-time / stats
 *----------------------------------------------------------*/
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/*-----------------------------------------------------------
 * Co-routines (unused)
 *----------------------------------------------------------*/
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

/*-----------------------------------------------------------
 * Software timers
 *----------------------------------------------------------*/
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            512

/*-----------------------------------------------------------
 * Interrupt priorities.
 *
 * configMAX_SYSCALL_INTERRUPT_PRIORITY masks interrupts via BASEPRI, which only
 * exists on the Armv8-M RP2350. The RP2040's Cortex-M0+ has no BASEPRI (its port
 * masks with PRIMASK) and no use for either value.
 *----------------------------------------------------------*/
#if PICO_RP2350 && !defined(__riscv)
#define configKERNEL_INTERRUPT_PRIORITY         255
/* Only 16 has been tested for the RP2350 port (see port README). */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    16
#endif

/*-----------------------------------------------------------
 * Assertions
 *----------------------------------------------------------*/
#ifndef __ASSEMBLER__
#include <assert.h>
#define configASSERT( x )  assert( x )
#endif

/*-----------------------------------------------------------
 * Optional API inclusion
 *----------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1

#endif /* FREERTOS_CONFIG_H */
