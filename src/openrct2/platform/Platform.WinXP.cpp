/*****************************************************************************
 * OpenRCT2 - Windows XP Edition
 *
 * Windows XP Runtime Compatibility Primitives
 * Provides native implementations for synchronization primitives (Condition
 * Variables, SRW Locks, InitOnce, GetTickCount64) introduced in Windows Vista,
 * allowing modern C++20 standard library features to execute seamlessly on
 * Windows XP (NT 5.1).
 *****************************************************************************/

#if defined(_WIN32) && !defined(_WIN64)

#include <windows.h>
#include <cstdint>

extern "C" {

// Override DLL import pointers so the linker satisfies MinGW / GCC libstdc++
// references locally without emitting entries into the PE import address table for KERNEL32.DLL.
#define DLL_OVERRIDE(name, argbytes) \
    __attribute__((used)) typeof(name) *name##_ptr  asm("__imp__" #name "@" #argbytes) = name; \
    __attribute__((used)) typeof(name) *name##_ptr2 asm("_imp__"  #name "@" #argbytes) = name; \
    __attribute__((used)) typeof(name) *name##_ptr3 asm("__imp__" #name) = name; \
    __attribute__((used)) typeof(name) *name##_ptr4 asm("_imp__"  #name) = name; \
    __asm__(".globl _" #name "\n\t_" #name " = _" #name "@" #argbytes);

#ifndef CONDITION_VARIABLE_INIT
typedef struct _RTL_CONDITION_VARIABLE {
    PVOID Ptr;
} RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE;
typedef RTL_CONDITION_VARIABLE CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT {0}
#endif

#ifndef SRWLOCK_INIT
typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} RTL_SRWLOCK, *PRTL_SRWLOCK;
typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {0}
#endif

#ifndef INIT_ONCE_STATIC_INIT
typedef union _RTL_RUN_ONCE {
    PVOID Ptr;
} RTL_RUN_ONCE, *PRTL_RUN_ONCE;
typedef RTL_RUN_ONCE INIT_ONCE, *PINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {0}
typedef BOOL (WINAPI *PINIT_ONCE_FN)(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context);
#endif

// ============================================================================
// Condition Variables for Windows XP
// Implemented via Win32 Events and a waiter list guarded by a Critical Section
// ============================================================================

struct CVWaiter {
    HANDLE event;
    CVWaiter* next;
    bool signaled;
};

static CRITICAL_SECTION g_cv_cs;
static volatile LONG g_cv_inited = 0;

static void EnsureCvInit()
{
    if (InterlockedCompareExchange(&g_cv_inited, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_cv_cs);
        InterlockedExchange(&g_cv_inited, 2);
    }
    else
    {
        while (g_cv_inited != 2)
            Sleep(0);
    }
}

VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (ConditionVariable)
        ConditionVariable->Ptr = NULL;
}
DLL_OVERRIDE(InitializeConditionVariable, 4)

BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection, DWORD dwMilliseconds)
{
    if (!ConditionVariable || !CriticalSection)
        return FALSE;

    EnsureCvInit();

    HANDLE hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!hEvent)
        return FALSE;

    CVWaiter waiter;
    waiter.event = hEvent;
    waiter.next = NULL;
    waiter.signaled = false;

    EnterCriticalSection(&g_cv_cs);
    CVWaiter** pp = (CVWaiter**)&ConditionVariable->Ptr;
    while (*pp)
        pp = &((*pp)->next);
    *pp = &waiter;
    LeaveCriticalSection(&g_cv_cs);

    LeaveCriticalSection(CriticalSection);

    DWORD waitRes = WaitForSingleObject(hEvent, dwMilliseconds);
    BOOL success = TRUE;

    if (waitRes != WAIT_OBJECT_0)
    {
        EnterCriticalSection(&g_cv_cs);
        if (!waiter.signaled)
        {
            pp = (CVWaiter**)&ConditionVariable->Ptr;
            while (*pp)
            {
                if (*pp == &waiter)
                {
                    *pp = waiter.next;
                    break;
                }
                pp = &((*pp)->next);
            }
            success = FALSE;
            SetLastError(waitRes == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_GEN_FAILURE);
        }
        else
        {
            success = TRUE;
        }
        LeaveCriticalSection(&g_cv_cs);
    }

    EnterCriticalSection(CriticalSection);
    CloseHandle(hEvent);
    return success;
}
DLL_OVERRIDE(SleepConditionVariableCS, 12)

VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (!ConditionVariable || !ConditionVariable->Ptr)
        return;

    EnsureCvInit();
    EnterCriticalSection(&g_cv_cs);
    CVWaiter* waiter = (CVWaiter*)ConditionVariable->Ptr;
    if (waiter)
    {
        ConditionVariable->Ptr = waiter->next;
        waiter->signaled = true;
        SetEvent(waiter->event);
    }
    LeaveCriticalSection(&g_cv_cs);
}
DLL_OVERRIDE(WakeConditionVariable, 4)

VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
    if (!ConditionVariable || !ConditionVariable->Ptr)
        return;

    EnsureCvInit();
    EnterCriticalSection(&g_cv_cs);
    CVWaiter* waiter = (CVWaiter*)ConditionVariable->Ptr;
    ConditionVariable->Ptr = NULL;
    while (waiter)
    {
        waiter->signaled = true;
        SetEvent(waiter->event);
        waiter = waiter->next;
    }
    LeaveCriticalSection(&g_cv_cs);
}
DLL_OVERRIDE(WakeAllConditionVariable, 4)

// ============================================================================
// Slim Reader/Writer (SRW) Locks for Windows XP
// Implemented via a pool of Critical Sections hashed by pointer address
// ============================================================================

#define SRW_HASH_SIZE 64
static CRITICAL_SECTION g_srw_locks[SRW_HASH_SIZE];
static volatile LONG g_srw_inited = 0;

static void EnsureSrwInit()
{
    if (InterlockedCompareExchange(&g_srw_inited, 1, 0) == 0)
    {
        for (int i = 0; i < SRW_HASH_SIZE; i++)
            InitializeCriticalSection(&g_srw_locks[i]);
        InterlockedExchange(&g_srw_inited, 2);
    }
    else
    {
        while (g_srw_inited != 2)
            Sleep(0);
    }
}

static inline CRITICAL_SECTION* GetSrwCS(PSRWLOCK srw)
{
    EnsureSrwInit();
    uintptr_t val = (uintptr_t)srw;
    size_t idx = ((val >> 3) ^ (val >> 9)) % SRW_HASH_SIZE;
    return &g_srw_locks[idx];
}

VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock)
{
    if (SRWLock)
        SRWLock->Ptr = NULL;
}
DLL_OVERRIDE(InitializeSRWLock, 4)

VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    EnterCriticalSection(GetSrwCS(SRWLock));
}
DLL_OVERRIDE(AcquireSRWLockExclusive, 4)

VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock)
{
    LeaveCriticalSection(GetSrwCS(SRWLock));
}
DLL_OVERRIDE(ReleaseSRWLockExclusive, 4)

VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock)
{
    EnterCriticalSection(GetSrwCS(SRWLock));
}
DLL_OVERRIDE(AcquireSRWLockShared, 4)

VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock)
{
    LeaveCriticalSection(GetSrwCS(SRWLock));
}
DLL_OVERRIDE(ReleaseSRWLockShared, 4)

BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    return TryEnterCriticalSection(GetSrwCS(SRWLock)) ? TRUE : FALSE;
}
DLL_OVERRIDE(TryAcquireSRWLockExclusive, 4)

BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock)
{
    return TryEnterCriticalSection(GetSrwCS(SRWLock)) ? TRUE : FALSE;
}
DLL_OVERRIDE(TryAcquireSRWLockShared, 4)

BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK SRWLock, DWORD dwMilliseconds, ULONG Flags)
{
    (void)Flags;
    CRITICAL_SECTION* cs = GetSrwCS(SRWLock);
    return SleepConditionVariableCS(ConditionVariable, cs, dwMilliseconds);
}
DLL_OVERRIDE(SleepConditionVariableSRW, 16)

// ============================================================================
// One-Time Initialization (InitOnce) for Windows XP
// ============================================================================

VOID WINAPI InitOnceInitialize(PINIT_ONCE InitOnce)
{
    if (InitOnce)
        InitOnce->Ptr = NULL;
}
DLL_OVERRIDE(InitOnceInitialize, 4)

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context)
{
    if (!InitOnce || !InitFn)
        return FALSE;

    for (;;)
    {
        uintptr_t val = (uintptr_t)InitOnce->Ptr;
        if ((val & 3) == 2)
        {
            if (Context)
                *Context = (LPVOID)(val & ~3);
            return TRUE;
        }

        if (val == 0)
        {
            if (InterlockedCompareExchangePointer((PVOID volatile *)&InitOnce->Ptr, (PVOID)1, (PVOID)0) == (PVOID)0)
            {
                PVOID out_ctx = NULL;
                if (InitFn(InitOnce, Parameter, &out_ctx))
                {
                    uintptr_t comp_val = ((uintptr_t)out_ctx & ~3) | 2;
                    InterlockedExchangePointer((PVOID volatile *)&InitOnce->Ptr, (PVOID)comp_val);
                    if (Context)
                        *Context = (LPVOID)((uintptr_t)out_ctx & ~3);
                    return TRUE;
                }
                else
                {
                    InterlockedExchangePointer((PVOID volatile *)&InitOnce->Ptr, (PVOID)0);
                    return FALSE;
                }
            }
        }
        else
        {
            Sleep(1);
        }
    }
}
DLL_OVERRIDE(InitOnceExecuteOnce, 16)

// ============================================================================
// GetTickCount64 for Windows XP
// Tracks rollover of 32-bit GetTickCount()
// ============================================================================

ULONGLONG WINAPI GetTickCount64(VOID)
{
    static ULONGLONG high = 0;
    static DWORD last_low = 0;
    static CRITICAL_SECTION g_gtc_cs;
    static volatile LONG gtc_inited = 0;
    if (InterlockedCompareExchange(&gtc_inited, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_gtc_cs);
        InterlockedExchange(&gtc_inited, 2);
    }
    else
    {
        while (gtc_inited != 2)
            Sleep(0);
    }
    EnterCriticalSection(&g_gtc_cs);
    DWORD low = GetTickCount();
    if (low < last_low)
        high += 0x100000000ULL;
    last_low = low;
    ULONGLONG result = high | (ULONGLONG)low;
    LeaveCriticalSection(&g_gtc_cs);
    return result;
}
DLL_OVERRIDE(GetTickCount64, 0)

// ============================================================================
// CancelIoEx for Windows XP
// ============================================================================

BOOL WINAPI CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped)
{
    (void)lpOverlapped;
    return CancelIo(hFile);
}
DLL_OVERRIDE(CancelIoEx, 8)

} // extern "C"

#endif // _WIN32
