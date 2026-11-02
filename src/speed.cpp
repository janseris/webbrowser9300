// Browser 9300 - Tools > Speed test (about:speed): how long the phone takes for the operations a
// JavaScript engine leans on. Every JS number is a double, and the 9300 has no FPU: GCC's double
// operations call EUSER's emulation (__adddf3, __muldf3, ...), whose speed we can't measure in the
// emulator (the SDK has only its import library). Each operation runs in a loop that doubles until
// it takes 0.3 s (the clock ticks every 15.6 ms), less the same loop without it.

#include <e32std.h>
#include "browser.h"

static TInt NowUs()
    {
    TTime t;
    t.UniversalTime();
    return (TInt)(t.Int64().Low() & 0x7fffffffu);        // (the low word: differences of < 30 min)
    }

// the loops: separate functions, the values volatile so nothing is worked out at compile time
static void LoopDblBase(TInt n) { volatile double a = 1.5, r; for (TInt i = 0; i < n; i++) r = a; (void)r; }
static void LoopDblAdd(TInt n) { volatile double a = 1.5, b = 2.25, r; for (TInt i = 0; i < n; i++) r = a + b; (void)r; }
static void LoopDblSub(TInt n) { volatile double a = 7.0, b = 2.25, r; for (TInt i = 0; i < n; i++) r = a - b; (void)r; }
static void LoopDblMul(TInt n) { volatile double a = 1.5, b = 2.25, r; for (TInt i = 0; i < n; i++) r = a * b; (void)r; }
static void LoopDblDiv(TInt n) { volatile double a = 1.5, b = 2.25, r; for (TInt i = 0; i < n; i++) r = a / b; (void)r; }
static void LoopIntToDbl(TInt n) { volatile TInt v = 12345; volatile double r; for (TInt i = 0; i < n; i++) r = (double)v; (void)r; }
static void LoopDblToInt(TInt n) { volatile double a = 12345.5; volatile TInt v; for (TInt i = 0; i < n; i++) v = (TInt)a; (void)v; }
static void LoopDblCmp(TInt n) { volatile double a = 1.5, b = 2.25; volatile TInt v; for (TInt i = 0; i < n; i++) v = a < b; (void)v; }
static void LoopIntBase(TInt n) { volatile TInt x = 1000003, y = 7, v; for (TInt i = 0; i < n; i++) v = x; (void)y; (void)v; }
static void LoopIntDiv(TInt n) { volatile TInt x = 1000003, y = 7, v; for (TInt i = 0; i < n; i++) v = x / y; (void)v; }

// the time of one round in ns (x 10, for a decimal place), the loop doubled until it takes 0.3 s
static TInt TimeLoop(void (*aLoop)(TInt))
    {
    TInt n = 1000;
    for (;;)
        {
        TInt t0 = NowUs();
        aLoop(n);
        TInt us = NowUs() - t0;
        if (us < 0) continue;                   // (the clock's low word wrapped: again)
        if (us >= 300000 || n > (1 << 26))
            {
            // ns x 10 a round: us * 10000 / n without overflow
            TInt64 v = TInt64(us) * TInt64(10000);
            v = v / TInt64(n);
            return v.GetTInt();
            }
        n *= us < 20000 ? 16 : 2;
        }
    }

// the time to copy 4 KB, in us x 10
static TInt TimeCopy(TBool aOurs, TUint8* aSrc, TUint8* aDst)
    {
    TInt n = 16;
    for (;;)
        {
        TInt t0 = NowUs();
        for (TInt i = 0; i < n; i++)
            {
            if (aOurs) ig_memcpy(aDst, aSrc, 4096); else Mem::Copy(aDst, aSrc, 4096);
            }
        TInt us = NowUs() - t0;
        if (us >= 300000 || n > (1 << 22)) return us * 10 / n;
        n *= us < 20000 ? 16 : 2;
        }
    }

static void Row(TDes8& aP, const TDesC8& aName, TInt aNs10, TInt aEmuInstr)
    {
    if (aNs10 < 0) aNs10 = 0;
    aP.Append(_L8("<tr><td>"));
    aP.Append(aName);
    aP.Append(_L8("</td><td align=right>"));
    aP.AppendNum(aNs10 / 10); aP.Append('.'); aP.AppendNum(aNs10 % 10);
    aP.Append(_L8(" ns</td><td align=right>"));
    aP.AppendNum(aNs10 * 15 / 100);         // cycles at 150 MHz: ns x 0.15
    aP.Append(_L8("</td><td align=right>"));
    if (aEmuInstr) aP.AppendNum(aEmuInstr); else aP.Append('-');
    aP.Append(_L8("</td></tr>"));
    }

HBufC8* BrSpeedTestPageLC()
    {
    TInt base = TimeLoop(LoopDblBase);
    TInt add = TimeLoop(LoopDblAdd) - base;
    TInt sub = TimeLoop(LoopDblSub) - base;
    TInt mul = TimeLoop(LoopDblMul) - base;
    TInt div = TimeLoop(LoopDblDiv) - base;
    TInt i2d = TimeLoop(LoopIntToDbl) - base;
    TInt d2i = TimeLoop(LoopDblToInt) - base;
    TInt cmp = TimeLoop(LoopDblCmp) - base;
    TInt ibase = TimeLoop(LoopIntBase);
    TInt idiv = TimeLoop(LoopIntDiv) - ibase;
    TInt ours = -1, sys = -1;
    TUint8* a = (TUint8*)User::Alloc(8192);
    if (a)
        {
        Mem::Fill(a, 8192, 0x5a);
        ours = TimeCopy(ETrue, a, a + 4096);
        sys = TimeCopy(EFalse, a, a + 4096);
        User::Free(a);
        }
    if (IgLogEnabled())
        {
        TBuf<160> l;
        l.Format(_L("speed (ns x10): dbl add %d sub %d mul %d div %d i2d %d d2i %d cmp %d, int div %d; 4 KB copy (us x10) ours %d Mem::Copy %d"),
                 add, sub, mul, div, i2d, d2i, cmp, idiv, ours, sys);
        IgLog(l);
        IgLogFlush();
        }
    HBufC8* h = HBufC8::NewLC(4096);
    TPtr8 p = h->Des();
    p.Append(_L8("<html><head><title>Speed test</title></head><body><h1>Speed test</h1>"
                 "<p>The time of one operation on this phone (each timed for at least 0.3 s, less the same loop without it). "
                 "Every JavaScript number is a double, and the 9300 has no FPU: these go through EUSER's emulation.</p>"
                 "<table border=1 cellpadding=2><tr><th>Operation</th><th>Time</th><th>Cycles</th><th>GCC's own (instructions, emulator)</th></tr>"));
    Row(p, _L8("double add"), add, 264);
    Row(p, _L8("double subtract"), sub, 286);
    Row(p, _L8("double multiply"), mul, 523);
    Row(p, _L8("double divide"), div, 1454);
    Row(p, _L8("int to double"), i2d, 712);
    Row(p, _L8("double to int"), d2i, 95);
    Row(p, _L8("double compare"), cmp, 176);
    Row(p, _L8("int divide"), idiv, 0);
    p.Append(_L8("</table><p>4 KB copy: the browser's assembly "));
    if (ours >= 0) { p.AppendNum(ours / 10); p.Append('.'); p.AppendNum(ours % 10); p.Append(_L8(" us")); } else p.Append('-');
    p.Append(_L8(", Mem::Copy "));
    if (sys >= 0) { p.AppendNum(sys / 10); p.Append('.'); p.AppendNum(sys % 10); p.Append(_L8(" us")); } else p.Append('-');
    p.Append(_L8(".</p><p>Send it with Tools &gt; Send a screenshot, or the log (with Logging on).</p></body></html>"));
    return h;
    }
