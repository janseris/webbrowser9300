// Browser 9300 - JavaScript (MuJS, js/mujs, built as mujs.lib with js/shim and js/bwlibc.c).
//
// Here: the platform half of MuJS's C library (the heap, Math::, the clock and the time zone), the
// thread scripts run on, and Tools > JavaScript test (about:js).
//
// The thread: the app's own stack is a few KB, and MuJS needs ~35 KB for a regular expression and
// ~150 bytes for each nested call (1024 deep at most: ~150 KB), so scripts run on a thread with a
// 256 KB stack that shares the app's heap. The app's thread waits for it (nothing else touches the
// heap meanwhile). A script that crashes kills only that thread; the browser reports it.
//
// mujs.h is not included: it pulls the shim's libc headers, whose macros (memcpy, free, ...) would
// clash with Symbian's. The few calls needed are declared below.

#include <e32std.h>
#include <e32math.h>
#include "browser.h"

extern "C"
    {
    struct js_State;
    typedef void* (*js_Alloc)(void* memctx, void* ptr, int size);
    js_State* js_newstate(js_Alloc alloc, void* actx, int flags);
    void js_freestate(js_State* J);
    void js_setlimit(js_State* J, int runlimit, int memlimit);
    void js_setlimithook(js_State* J, int (*hook)(void* ctx), void* ctx);
    int js_ploadstring(js_State* J, const char* filename, const char* source);
    int js_pcall(js_State* J, int n);
    void js_pushundefined(js_State* J);
    const char* js_tostring(js_State* J, int idx);
    void js_pop(js_State* J, int n);
    void js_gc(js_State* J, int report);

    // ---- MuJS's C library, the platform half (js/shim/bwlibc.h)
    void* bwc_malloc(unsigned int n) { return User::Alloc(n); }
    void* bwc_realloc(void* p, unsigned int n) { return User::ReAlloc(p, n); }
    void bwc_free(void* p) { User::Free(p); }
    void bwc_abort(void) { User::Panic(_L("BrJs"), 1); }
    long bwc_time(long* t)
        {
        long s = (long)NowUnixSeconds();
        if (t) *t = s;
        return s;
        }
    // ms since 1970 UTC (Date.now)
    double js_platform_now(void)
        {
        TTime now, epoch(TDateTime(1970, EJanuary, 0, 0, 0, 0, 0));
        now.UniversalTime();
        TInt64 ms = now.MicroSecondsFrom(epoch).Int64() / TInt64(1000);
        return ms.GetTReal();
        }
    // the phone's offset from UTC (with summer time when it is on), in ms
    double js_platform_tza(void)
        {
        TLocale loc;
        return (double)loc.UniversalTimeOffset().Int() * 1000.0;
        }

    // ---- the Math:: primitives: js/bwlibc.c settles NaN, infinities, zeros and the domains first
    static double MathRes(TInt aErr, TReal aRes, TReal aBig)
        {
        if (aErr == KErrOverflow) return aBig;
        if (aErr == KErrUnderflow) return 0.0;
        return aRes;
        }
    double bwp_sqrt(double x) { TReal r = 0; TInt e = Math::Sqrt(r, x); return MathRes(e, r, x); }
    double bwp_exp(double x) { TReal r = 0; TInt e = Math::Exp(r, x); return MathRes(e, r, 1.0 / 0.0); }
    double bwp_log(double x) { TReal r = 0; TInt e = Math::Ln(r, x); return MathRes(e, r, r); }
    double bwp_pow(double x, double y) { TReal r = 0; TInt e = Math::Pow(r, x, y); return MathRes(e, r, 1.0 / 0.0); }
    double bwp_sin(double x) { TReal r = 0; Math::Sin(r, x); return r; }
    double bwp_cos(double x) { TReal r = 0; Math::Cos(r, x); return r; }
    double bwp_tan(double x) { TReal r = 0; Math::Tan(r, x); return r; }
    double bwp_asin(double x) { TReal r = 0; Math::ASin(r, x); return r; }
    double bwp_acos(double x) { TReal r = 0; Math::ACos(r, x); return r; }
    double bwp_atan(double x) { TReal r = 0; Math::ATan(r, x); return r; }
    double bwp_atan2(double y, double x) { TReal r = 0; Math::ATan(r, y, x); return r; }
    }

// ------------------------------------------------------------------ one engine's heap and clock

const TInt KJsStack = 256 * 1024;
const TInt KJsRamReserve = 512 * 1024;     // the free RAM scripts leave (as the caches do)
const TInt KJsMaxHeap = 16 * 1024 * 1024;
const TInt KJsSlice = 100000;              // instructions between looks at the clock (~10 ms)

// ms for differences (the low word of the clock in ms: differences are right across its wrap;
// TickMs rounds the 15.625 ms tick down to 15)
static TUint32 JsMs()
    {
    TTime t;
    t.UniversalTime();
    return (t.Int64() / TInt64(1000)).Low();
    }

struct TJsCtx
    {
    TInt iLive;          // bytes the engine holds
    TInt iPeak;
    TInt iLimit;
    TUint32 iDeadline;   // JsMs
    };

// MuJS's allocator: User::ReAlloc, with the bytes counted against the limit (NULL: MuJS throws
// "out of memory", the script stops, the browser goes on)
static void* JsAlloc(void* aCtx, void* aPtr, int aSize)
    {
    TJsCtx* c = (TJsCtx*)aCtx;
    TInt old = aPtr ? User::AllocLen(aPtr) : 0;
    if (aSize == 0)
        {
        if (aPtr) { c->iLive -= old; User::Free(aPtr); }
        return NULL;
        }
    if (aSize > old && c->iLive - old + aSize > c->iLimit) return NULL;
    void* q = aPtr ? User::ReAlloc(aPtr, aSize) : User::Alloc(aSize);
    if (!q) return NULL;
    c->iLive += User::AllocLen(q) - old;
    if (c->iLive > c->iPeak) c->iPeak = c->iLive;
    return q;
    }

// asked every KJsSlice instructions: more while the time lasts
static int JsLimitHook(void* aCtx)
    {
    TJsCtx* c = (TJsCtx*)aCtx;
    return (TInt)(c->iDeadline - JsMs()) > 0 ? KJsSlice : 0;
    }

static void JsInitCtx(TJsCtx& aCtx, TInt aMs)
    {
    aCtx.iLive = 0;
    aCtx.iPeak = 0;
    TInt room = BrFreeRam() - KJsRamReserve;
    aCtx.iLimit = room < KJsMaxHeap ? room : KJsMaxHeap;
    if (aCtx.iLimit < 0) aCtx.iLimit = 0;
    aCtx.iDeadline = JsMs() + aMs;
    }

static js_State* JsNew(TJsCtx& aCtx)
    {
    js_State* J = js_newstate(JsAlloc, &aCtx, 0);
    if (!J) return NULL;
    js_setlimit(J, KJsSlice, 0);
    js_setlimithook(J, JsLimitHook, &aCtx);
    return J;
    }

// ------------------------------------------------------------------ the thread

struct TJsThreadJob
    {
    void (*iFunc)(TAny* aArg);
    TAny* iArg;
    };

// EUSER's soft float raises an exception where IEEE gives an infinity or a NaN (1/0, 0/0, an
// overflow), and a thread without a handler panics USER-EXEC 3. JavaScript needs those values:
// the handler does nothing, the operation goes on and returns the IEEE result. (An integer divide
// by zero is ignored too, rather than ending the scripts.)
static void JsIgnoreException(TExcType /*aType*/)
    {
    }

static TInt JsThreadMain(TAny* aJob)
    {
    TJsThreadJob* job = (TJsThreadJob*)aJob;
    RThread().SetExceptionHandler(JsIgnoreException, KExceptionFpe | KExceptionInteger);
    CTrapCleanup* tc = CTrapCleanup::New();
    if (!tc) return KErrNoMemory;
    job->iFunc(job->iArg);
    delete tc;
    return KErrNone;
    }

// Runs aFunc(aArg) on the big-stack thread and waits for it. KErrNone, or why it did not finish:
// aWhy gets "KERN-EXEC 3" and so on for a crash.
static TInt RunOnJsThread(void (*aFunc)(TAny*), TAny* aArg, TDes& aWhy)
    {
    TJsThreadJob job;
    job.iFunc = aFunc;
    job.iArg = aArg;
    RThread t;
    TBuf<24> name;
    name.Format(_L("br9300js%u"), (TUint)User::TickCount());
    TInt err = t.Create(name, JsThreadMain, KJsStack, &User::Heap(), &job);
    if (err != KErrNone) { aWhy.Format(_L("no thread (%d)"), err); return err; }
    TRequestStatus st;
    t.Logon(st);
    t.Resume();
    User::WaitForRequest(st);
    if (t.ExitType() == EExitPanic)
        {
        TExitCategoryName cat = t.ExitCategory();
        aWhy.Format(_L("crashed: %S %d"), &cat, t.ExitReason());
        err = KErrDied;
        }
    else if (st.Int() != KErrNone) { aWhy.Format(_L("ended with %d"), st.Int()); err = st.Int(); }
    t.Close();
    return err;
    }

// ------------------------------------------------------------------ about:js

#include "jstests.h"

struct TJsTestResult
    {
    TInt iMs;
    TInt iPeakKb;
    TInt iOk;            // 1 right, 0 wrong, -1 not run
    char iGot[160];
    };

struct TJsTestRun
    {
    TJsTestResult iRes[KJsTestCount];
    char iInfo[200];     // the clock and the time zone, as the engine sees them
    TInt iStartKb;
    };

static void CopyC(char* aDst, TInt aMax, const char* aSrc)
    {
    TInt i = 0;
    if (aSrc) for (; aSrc[i] && i < aMax - 1; i++) aDst[i] = aSrc[i];
    aDst[i] = 0;
    }

static TBool SameC(const char* a, const char* b)
    {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
    }

static void JsTestsOnThread(TAny* aRun)
    {
    TJsTestRun* run = (TJsTestRun*)aRun;
    for (TInt i = 0; i < KJsTestCount; i++)
        {
        TJsTestResult& r = run->iRes[i];
        const TJsTest& t = KJsTests[i];
        TJsCtx ctx;
        // the runaway one is stopped after 2 s; the others have a minute
        TBool runaway = SameC(t.iName, "c_runaway");
        JsInitCtx(ctx, runaway ? 2000 : 60000);
        js_State* J = JsNew(ctx);
        if (!J) { CopyC(r.iGot, sizeof r.iGot, "(no memory for the engine)"); r.iOk = 0; continue; }
        TUint32 t0 = JsMs();
        const char* res;
        char buf[160];
        if (js_ploadstring(J, t.iName, t.iSource)) { CopyC(buf, sizeof buf, "compile: "); res = js_tostring(J, -1); }
        else
            {
            js_pushundefined(J);
            if (js_pcall(J, 0)) { CopyC(buf, sizeof buf, "error: "); res = js_tostring(J, -1); }
            else { buf[0] = 0; res = js_tostring(J, -1); }
            }
        r.iMs = (TInt)(JsMs() - t0);
        TInt n = 0;
        while (buf[n]) n++;
        CopyC(buf + n, sizeof buf - n, res);
        CopyC(r.iGot, sizeof r.iGot, buf);
        r.iOk = SameC(buf, t.iExpect) ? 1 : 0;
        r.iPeakKb = ctx.iPeak / 1024;
        js_freestate(J);
        }
    // what Date says here (not checked: it depends on the clock and the settings)
    TJsCtx ctx;
    JsInitCtx(ctx, 5000);
    js_State* J = JsNew(ctx);
    if (J)
        {
        if (js_ploadstring(J, "info", "var d = new Date(); d.toString() + ' (UTC offset ' + (-d.getTimezoneOffset()) + ' min)'") == 0)
            {
            js_pushundefined(J);
            js_pcall(J, 0);
            CopyC(run->iInfo, sizeof run->iInfo, js_tostring(J, -1));
            }
        js_freestate(J);
        }
    }

// UTF-8 text into the page, HTML-escaped (non-ASCII as character references)
static void AppendHtmlUtf8(TDes8& aP, const char* aS)
    {
    const TUint8* s = (const TUint8*)aS;
    while (*s && aP.Length() < aP.MaxLength() - 16)
        {
        TUint c = *s++;
        if (c >= 0xC0 && (*s & 0xC0) == 0x80)
            {
            TInt more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            c &= 0x3F >> more;
            while (more-- && (*s & 0xC0) == 0x80) c = (c << 6) | (*s++ & 0x3F);
            }
        if (c == '<') aP.Append(_L8("&lt;"));
        else if (c == '>') aP.Append(_L8("&gt;"));
        else if (c == '&') aP.Append(_L8("&amp;"));
        else if (c < 0x80) aP.Append((TChar)c);
        else { aP.Append(_L8("&#")); aP.AppendNum(c); aP.Append(';'); }
        }
    }

HBufC8* BrJsTestPageLC()
    {
    TJsTestRun* run = (TJsTestRun*)User::Alloc(sizeof(TJsTestRun));
    if (!run) User::Leave(KErrNoMemory);
    CleanupStack::PushL(run);
    Mem::FillZ(run, sizeof(TJsTestRun));
    for (TInt i = 0; i < KJsTestCount; i++) run->iRes[i].iOk = -1;
    TBuf<80> why;
    TUint32 t0 = JsMs();
    TInt err = RunOnJsThread(JsTestsOnThread, run, why);
    TInt totalMs = (TInt)(JsMs() - t0);

    HBufC8* h = HBufC8::NewLC(16384 + KJsTestCount * 600);
    TPtr8 p = h->Des();
    TInt good = 0, done = 0;
    for (TInt i = 0; i < KJsTestCount; i++) { if (run->iRes[i].iOk >= 0) done++; if (run->iRes[i].iOk == 1) good++; }
    p.Append(_L8("<html><head><title>JavaScript test</title></head><body><h1>JavaScript test</h1>"
                 "<p>The JavaScript engine (MuJS, ES5) runs these scripts on this phone and checks the results against Node's. "
                 "Pages do not run scripts yet: this checks the engine first.</p><p><b>"));
    p.AppendNum(good); p.Append(_L8(" of ")); p.AppendNum(KJsTestCount); p.Append(_L8(" right</b>, "));
    p.AppendNum(totalMs / 1000); p.Append('.'); p.AppendNum(totalMs % 1000 / 100); p.Append(_L8(" s in all."));
    if (err != KErrNone)
        {
        TBuf8<80> w;
        w.Copy(why);
        p.Append(_L8(" <b>The engine's thread "));
        p.Append(w);
        p.Append(_L8("</b> after "));
        p.AppendNum(done);
        p.Append(_L8(" tests."));
        }
    p.Append(_L8("</p><table border=1 cellpadding=2><tr><th>Test</th><th>Time</th><th>Heap</th><th>Result</th></tr>"));
    for (TInt i = 0; i < KJsTestCount; i++)
        {
        const TJsTestResult& r = run->iRes[i];
        p.Append(_L8("<tr><td>"));
        AppendHtmlUtf8(p, KJsTests[i].iName);
        p.Append(_L8("</td><td align=right>"));
        if (r.iOk >= 0) { p.AppendNum(r.iMs); p.Append(_L8(" ms")); } else p.Append('-');
        p.Append(_L8("</td><td align=right>"));
        if (r.iOk >= 0) { p.AppendNum(r.iPeakKb); p.Append(_L8(" KB")); } else p.Append('-');
        p.Append(_L8("</td><td>"));
        if (r.iOk == 1) { p.Append(_L8("ok: ")); AppendHtmlUtf8(p, r.iGot); }
        else if (r.iOk == 0)
            {
            p.Append(_L8("<b>WRONG</b>: "));
            AppendHtmlUtf8(p, r.iGot);
            p.Append(_L8("<br>expected: "));
            AppendHtmlUtf8(p, KJsTests[i].iExpect);
            }
        else p.Append(_L8("not run"));
        p.Append(_L8("</td></tr>"));
        }
    p.Append(_L8("</table><p>The clock as scripts see it: "));
    AppendHtmlUtf8(p, run->iInfo[0] ? run->iInfo : "-");
    p.Append(_L8("</p><p>Send it with Tools &gt; Send a screenshot, or the log (with Logging on).</p></body></html>"));

    if (IgLogEnabled())
        {
        TBuf<200> l;
        l.Format(_L("js test: %d of %d right, %d ms, thread %d"), good, KJsTestCount, totalMs, err);
        IgLog(l);
        for (TInt i = 0; i < KJsTestCount; i++)
            {
            const TJsTestResult& r = run->iRes[i];
            TBuf8<200> l8;
            l8.Format(_L8("js %s: %s, %d ms, %d KB: "), KJsTests[i].iName, r.iOk == 1 ? "ok" : r.iOk == 0 ? "WRONG" : "not run", r.iMs, r.iPeakKb);
            TPtrC8 got((const TUint8*)r.iGot);
            l8.Append(got.Left(l8.MaxLength() - l8.Length()));
            IgLog8(l8);
            }
        IgLogFlush();
        }
    CleanupStack::Pop(h);
    CleanupStack::PopAndDestroy(run);
    CleanupStack::PushL(h);
    return h;
    }
