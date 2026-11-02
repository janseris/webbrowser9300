// Browser 9300 - memory hooks for the C core, text conversion, time, the log (from Instagram 9300).

#include <e32std.h>
#include <hal.h>
#include "browser.h"

_LIT(KLogFile, "C:\\System\\Data\\browser9300\\log.txt");
_LIT(KLogPrev, "C:\\System\\Data\\browser9300\\log_prev.txt");

extern "C" void* ig_malloc(unsigned int aSize) { return User::Alloc((TInt)aSize); }
extern "C" void ig_free(void* aPtr) { User::Free(aPtr); }

HBufC* Utf8ToUnicodeL(const TDesC8& aText)
    {
    TInt n = (TInt)ig_utf8_to_utf16((const char*)aText.Ptr(), aText.Length(), NULL, 0);
    HBufC* buf = HBufC::NewL(n);
    TPtr p = buf->Des();
    ig_utf8_to_utf16((const char*)aText.Ptr(), aText.Length(), (unsigned short*)p.Ptr(), n);
    p.SetLength(n);
    return buf;
    }

HBufC* Utf8ToUnicodeL(const char* aText)
    {
    TPtrC8 p((const TUint8*)(aText ? aText : ""));
    return Utf8ToUnicodeL(p);
    }

HBufC8* UnicodeToUtf8L(const TDesC& aText)
    {
    unsigned int n = ig_utf16_to_utf8((const unsigned short*)aText.Ptr(), aText.Length(), NULL, 0);
    HBufC8* out = HBufC8::NewL(n);
    TPtr8 p = out->Des();
    ig_utf16_to_utf8((const unsigned short*)aText.Ptr(), aText.Length(), (char*)p.Ptr(), n);
    p.SetLength(n);
    return out;
    }

const TDesC& LogFileName() { return KLogFile; }
const TDesC& LogPrevFileName() { return KLogPrev; }

// The phone's free RAM plus what is free inside our heap (memory deleted there stays in the
// heap: the phone's count doesn't go up; Maps 9300's notes)
TInt BrFreeRam()
    {
    TInt freeRam = 0;
    HAL::Get(HALData::EMemoryRAMFree, freeRam);
    TInt biggest = 0;
    return freeRam + User::Available(biggest);
    }

TUint32 TickMs()
    {
    TInt period = 1000;
    HAL::Get(HALData::ESystemTickPeriod, period);      // microseconds per tick
    return (TUint32)(User::TickCount() * (TUint32)(period / 1000 > 0 ? period / 1000 : 1));
    }

char* UnicodeToUtf8(const TDesC& aText)
    {
    unsigned int n = ig_utf16_to_utf8((const unsigned short*)aText.Ptr(), aText.Length(), NULL, 0);
    char* out = (char*)ig_malloc(n + 1);
    if (!out) return NULL;
    ig_utf16_to_utf8((const unsigned short*)aText.Ptr(), aText.Length(), out, n);
    out[n] = 0;
    return out;
    }

static TTime UnixEpoch()
    {
    return TTime(TDateTime(1970, EJanuary, 0, 0, 0, 0, 0));
    }

TUint NowUnixSeconds()
    {
    TTime now;
    now.UniversalTime();
    TTimeIntervalSeconds s;
    if (now.SecondsFrom(UnixEpoch(), s) != KErrNone) return 0;
    return (TUint)s.Int();
    }


// ------------------------------------------------------------------ log
//
// Lines go to a RAM buffer (the flash is slow: ~15 ms a line). IgLogFlush() writes the whole
// buffer to the file at milestones; a panic loses only the lines after the last flush.

const TInt KLogMax = 60 * 1024;

// no writable static data in an EKA1 app: the state hangs off thread-local storage
struct TLogState
    {
    TBool iOn;
    HBufC8* iBuf;
    TBool iDirty;
    TInt iWritten;                 // the start of iBuf already in the file
    TBool iRewrite;                // iBuf was cut: the file is written anew
    RFs iFs;                       // kept open (a session a flush was a part of its cost)
    TBool iFsOpen;
    };

static TLogState* LogState()
    {
    TLogState* st = (TLogState*)Dll::Tls();
    if (!st)
        {
        st = new TLogState;
        if (!st) return NULL;
        st->iOn = EFalse;
        st->iBuf = HBufC8::New(KLogMax);
        st->iDirty = EFalse;
        st->iWritten = 0;
        st->iRewrite = ETrue;
        st->iFsOpen = EFalse;
        Dll::SetTls(st);
        }
    return st;
    }

void IgLogEnable(TBool aOn)
    {
    TLogState* st = LogState();
    if (st) st->iOn = aOn;
    }

TBool IgLogEnabled()
    {
    TLogState* st = (TLogState*)Dll::Tls();
    return st && st->iOn && st->iBuf;
    }

void IgLog8(const TDesC8& aText)
    {
    if (!IgLogEnabled()) return;
    TLogState* st = LogState();
    TPtr8 p = st->iBuf->Des();
    if (p.Length() + aText.Length() + 20 > p.MaxLength())
        {
        // keep the newest half
        TInt cut = p.Length() / 2;
        p.Delete(0, cut);
        p.Insert(0, _L8("...\r\n"));
        st->iRewrite = ETrue;
        }
    TTime now;
    now.HomeTime();
    TDateTime dt = now.DateTime();
    TBuf8<16> stamp;
    stamp.Format(_L8("%02d:%02d:%02d.%d "), dt.Hour(), dt.Minute(), dt.Second(), dt.MicroSecond() / 100000);
    p.Append(stamp);
    p.Append(aText.Left(300));
    p.Append(_L8("\r\n"));
    st->iDirty = ETrue;
    }

// Only what was added since the last flush is appended (0.29 and before wrote the whole buffer,
// up to 60 KB, at every trace: ~0.7 s each with the log on); the whole file again after a cut.
void IgLogFlush()
    {
    TLogState* st = (TLogState*)Dll::Tls();
    if (!st || !st->iBuf || !st->iDirty) return;
    if (!st->iFsOpen)
        {
        if (st->iFs.Connect() != KErrNone) return;
        st->iFsOpen = ETrue;
        }
    RFile f;
    TInt len = st->iBuf->Length();
    TBool whole = st->iRewrite || st->iWritten > len;
    TInt err = KErrGeneral;
    if (!whole)
        {
        err = f.Open(st->iFs, KLogFile, EFileWrite | EFileShareAny);
        if (err == KErrNone)
            {
            TInt pos = 0;
            err = f.Seek(ESeekEnd, pos);
            if (err == KErrNone) err = f.Write(st->iBuf->Mid(st->iWritten));
            f.Close();
            }
        }
    if (whole || err != KErrNone)
        {
        err = f.Replace(st->iFs, KLogFile, EFileWrite | EFileShareAny);
        if (err != KErrNone) { st->iFs.MkDirAll(KLogFile); err = f.Replace(st->iFs, KLogFile, EFileWrite | EFileShareAny); }
        if (err == KErrNone)
            {
            err = f.Write(*st->iBuf);
            f.Close();
            }
        }
    if (err == KErrNone)
        {
        st->iWritten = len;
        st->iRewrite = EFalse;
        st->iDirty = EFalse;
        }
    }

void IgLogClose()
    {
    IgLogFlush();
    TLogState* st = (TLogState*)Dll::Tls();
    if (st) { if (st->iFsOpen) st->iFs.Close(); delete st->iBuf; delete st; Dll::SetTls(NULL); }
    }

// (heap buffers: the log is used deep in call chains and the stack is small)
void IgTrace(const TDesC& aStep)
    {
    if (!IgLogEnabled()) return;
    TBuf<80> t(_L("trace: "));
    t.Append(aStep.Left(72));
    IgLog(t);
    IgLogFlush();
    }

// The heap is checked first (RHeap::Check panics on a corrupt heap): "heap check: X" in the log
// without "X" after it means the heap was already broken before X.
void IgTraceHeap(const TDesC& aStep)
    {
    if (!IgLogEnabled()) return;
    TBuf<80> t(_L("heap check: "));
    t.Append(aStep.Left(60));
    IgTrace(t);
    User::Heap().Check();
    IgTrace(aStep);
    }

void IgTraceF(const TDesC& aFormat, TInt aValue)
    {
    if (!IgLogEnabled()) return;
    TBuf<80> t;
    t.Format(aFormat, aValue);
    IgTrace(t);
    }

void IgLog(const TDesC& aText)
    {
    if (!IgLogEnabled()) return;
    HBufC8* t = HBufC8::New(300);
    if (!t) return;
    t->Des().Copy(aText.Left(300));
    IgLog8(*t);
    delete t;
    }

void IgLogF(const TDesC& aFormat, TInt aValue)
    {
    if (!IgLogEnabled()) return;
    HBufC* t = HBufC::New(200);
    if (!t) return;
    t->Des().Format(aFormat, aValue);
    IgLog(*t);
    delete t;
    }

void IgLogF2(const TDesC& aFormat, TInt aValue1, TInt aValue2)
    {
    if (!IgLogEnabled()) return;
    HBufC* t = HBufC::New(200);
    if (!t) return;
    t->Des().Format(aFormat, aValue1, aValue2);
    IgLog(*t);
    delete t;
    }

void IgLogRotate()
    {
    RFs fs;
    if (fs.Connect() != KErrNone) return;
    fs.Delete(KLogPrev);
    fs.Rename(KLogFile, KLogPrev);
    fs.Close();
    }
