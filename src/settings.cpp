// Browser 9300 - settings.txt: key=value lines in C:\System\Data\browser9300 (one per setting)

#include <e32std.h>
#include "browser.h"

_LIT(KSettingsFile, "C:\\System\\Data\\browser9300\\settings.txt");

CBrSettings* CBrSettings::NewL(RFs& aFs)
    {
    CBrSettings* self = new (ELeave) CBrSettings(aFs);
    return self;
    }

CBrSettings::CBrSettings(RFs& aFs) : iFs(aFs)
    {
    iImages = ETrue;
    iStyles = ETrue;
    iLogOn = ETrue;
    iMouseOn = ETrue;
    iFullScreen = ETrue;
    iTextSize = 0;
    iFullSize = EFalse;
    iStatsOn = ETrue;
    iPromptAccessPoint = EFalse;
    iHome.Copy(_L("about:start"));
    iServerHost.Copy(_L8("192.168.137.1"));
    iServerPort = 8000;
    }

void CBrSettings::LoadL()
    {
    RFile f;
    if (f.Open(iFs, KSettingsFile, EFileRead) != KErrNone) return;
    CleanupClosePushL(f);
    TInt size = 0;
    f.Size(size);
    if (size > 4096) size = 4096;
    HBufC8* buf = HBufC8::NewLC(size);
    TPtr8 p = buf->Des();
    User::LeaveIfError(f.Read(p));
    TLex8 lex(p);
    while (!lex.Eos())
        {
        lex.Mark();
        while (!lex.Eos() && lex.Peek() != '\n') lex.Inc();
        TPtrC8 line = lex.MarkedToken();
        if (!lex.Eos()) lex.Inc();
        if (line.Length() && line[line.Length() - 1] == '\r') line.Set(line.Left(line.Length() - 1));
        TInt eq = line.Locate('=');
        if (eq <= 0) continue;
        TPtrC8 key = line.Left(eq);
        TPtrC8 val = line.Mid(eq + 1);
        if (key == _L8("images")) iImages = val != _L8("0");
        else if (key == _L8("styles")) iStyles = val != _L8("0");
        else if (key == _L8("log")) iLogOn = val != _L8("0");
        else if (key == _L8("pointer")) iMouseOn = val == _L8("1");   // "mouse" (off by default before 0.8) is ignored
        else if (key == _L8("full")) iFullScreen = val == _L8("1");
        else if (key == _L8("fullsize")) iFullSize = val == _L8("1");
        else if (key == _L8("stats")) iStatsOn = val == _L8("1");
        else if (key == _L8("prompt")) iPromptAccessPoint = val == _L8("1");
        else if (key == _L8("textsize")) { TLex8 l(val); TInt v; if (l.Val(v) == KErrNone && v >= -1 && v <= 1) iTextSize = v; }
        else if (key == _L8("home")) { HBufC* h = Utf8ToUnicodeL(val); iHome.Copy(h->Left(iHome.MaxLength())); delete h; }
        else if (key == _L8("server")) { TBuf<80> t; t.Copy(val.Left(80)); SetServer(t); }
        }
    CleanupStack::PopAndDestroy(2);
    }

void CBrSettings::Save()
    {
    iFs.MkDirAll(KSettingsFile);
    RFile f;
    if (f.Replace(iFs, KSettingsFile, EFileWrite) != KErrNone) { IgLog(_L("settings: cannot write")); return; }
    TBuf8<1200> d;
    d.Append(_L8("images=")); d.AppendNum(iImages ? 1 : 0);
    d.Append(_L8("\r\nstyles=")); d.AppendNum(iStyles ? 1 : 0);
    d.Append(_L8("\r\nlog=")); d.AppendNum(iLogOn ? 1 : 0);
    d.Append(_L8("\r\npointer=")); d.AppendNum(iMouseOn ? 1 : 0);
    d.Append(_L8("\r\nfull=")); d.AppendNum(iFullScreen ? 1 : 0);
    d.Append(_L8("\r\nfullsize=")); d.AppendNum(iFullSize ? 1 : 0);
    d.Append(_L8("\r\nstats=")); d.AppendNum(iStatsOn ? 1 : 0);
    d.Append(_L8("\r\nprompt=")); d.AppendNum(iPromptAccessPoint ? 1 : 0);
    d.Append(_L8("\r\ntextsize=")); d.AppendNum(iTextSize);
    d.Append(_L8("\r\nhome="));
    {
    HBufC8* h = NULL;
    TRAPD(err, h = UnicodeToUtf8L(iHome));
    if (err == KErrNone && h) { d.Append(h->Left(800)); delete h; }
    }
    d.Append(_L8("\r\nserver="));
    d.Append(iServerHost);
    d.Append(':');
    d.AppendNum(iServerPort);
    d.Append(_L8("\r\n"));
    f.Write(d);
    f.Close();
    }

// "host:port" or "http://host:port/" (port defaults to 8000)
void CBrSettings::SetServer(const TDesC& aText)
    {
    TPtrC t(aText);
    while (t.Length() && t[0] == ' ') t.Set(t.Mid(1));
    if (t.Left(7).CompareF(_L("http://")) == 0) t.Set(t.Mid(7));
    TInt slash = t.Locate('/');
    if (slash >= 0) t.Set(t.Left(slash));
    TInt colon = t.LocateReverse(':');
    TInt port = 8000;
    if (colon > 0)
        {
        TLex l(t.Mid(colon + 1));
        if (l.Val(port) != KErrNone || port <= 0 || port > 65535) port = 8000;
        t.Set(t.Left(colon));
        }
    while (t.Length() && t[t.Length() - 1] == ' ') t.Set(t.Left(t.Length() - 1));
    if (t.Length() == 0 || t.Length() > 64) return;
    iServerHost.Copy(t);
    iServerPort = port;
    }

void CBrSettings::ServerText(TDes& aText) const
    {
    aText.Copy(iServerHost);
    aText.Append(':');
    aText.AppendNum(iServerPort);
    }
