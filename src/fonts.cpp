// Browser 9300 - the 32 font ids of the core (size x bold x italic x mono) as phone fonts, and
// the metrics the layout measures with. Fonts are made when first used.

#include <eikenv.h>
#include <w32std.h>
#include "browser.h"

CBrFonts* CBrFonts::NewL(TInt aSizeStep)
    {
    return new (ELeave) CBrFonts(aSizeStep);
    }

CBrFonts::CBrFonts(TInt aSizeStep) : iStep(aSizeStep)
    {
    for (TInt i = 0; i < BW_FONT_COUNT; i++) iFonts[i] = NULL;
    iMetrics.ctx = this;
    iMetrics.textWidth = TextWidth;
    iMetrics.fontHeight = Height;
    iMetrics.fontAscent = Ascent;
    }

CBrFonts::~CBrFonts()
    {
    CWsScreenDevice* dev = CEikonEnv::Static()->ScreenDevice();
    for (TInt i = 0; i < BW_FONT_COUNT; i++)
        if (iFonts[i]) dev->ReleaseFont(iFonts[i]);
    }

const CFont* CBrFonts::Font(TInt aId)
    {
    if (aId < 0 || aId >= BW_FONT_COUNT) aId = BW_FONT_MAKE(1, 0);
    if (iFonts[aId]) return iFonts[aId];
    CEikonEnv* env = CEikonEnv::Static();
    const CFont* normal = env->NormalFont();
    const CFont* dense = env->DenseFont();
    TInt base = normal->HeightInPixels();
    TInt small = dense->HeightInPixels();
    if (small >= base) small = base - 2;
    TInt px;
    switch (BW_FONT_SIZE(aId))
        {
        case 0: px = small; break;
        case 2: px = base * 5 / 4; break;
        case 3: px = base * 3 / 2; break;
        default: px = base; break;
        }
    if (iStep < 0) px = px * 85 / 100;
    else if (iStep > 0) px = px * 6 / 5;
    if (px < 8) px = 8;
    TFontSpec spec = normal->FontSpecInTwips();
    if (aId & BW_FONT_MONO) spec.iTypeface.iName = _L("Courier");
    spec.iHeight = env->ScreenDevice()->VerticalPixelsToTwips(px);
    spec.iFontStyle.SetStrokeWeight((aId & BW_FONT_BOLD) ? EStrokeWeightBold : EStrokeWeightNormal);
    spec.iFontStyle.SetPosture((aId & BW_FONT_ITALIC) ? EPostureItalic : EPostureUpright);
    CFont* f = NULL;
    if (env->ScreenDevice()->GetNearestFontInTwips(f, spec) != KErrNone || !f)
        return normal;                 // (never released: not ours)
    iFonts[aId] = f;
    return f;
    }

int CBrFonts::TextWidth(void* ctx, int font, const unsigned short* s, int n)
    {
    CBrFonts* self = (CBrFonts*)ctx;
    if (n <= 0) return 0;
    TPtrC t((const TUint16*)s, n);
    return self->Font(font)->TextWidthInPixels(t);
    }

int CBrFonts::Height(void* ctx, int font)
    {
    CBrFonts* self = (CBrFonts*)ctx;
    return self->Font(font)->HeightInPixels() + 2;
    }

int CBrFonts::Ascent(void* ctx, int font)
    {
    CBrFonts* self = (CBrFonts*)ctx;
    return self->Font(font)->AscentInPixels() + 1;
    }
