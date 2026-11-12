// Ownership harness for 4D native strings, pictures and variables.
// Links the real plugin sources and the unchanged SDK 4DPluginAPI.c against a
// synthetic Call4D that tracks every engine-owned allocation. ASan/UBSan
// catch double frees and use-after-free; the registries catch leaks (LSan is
// unavailable on macOS).

#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include "4DPlugin-JSON.h"
#include "C_TYPES.h"
#include "zxing_stub.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>

extern "C" FOURDCALL FourDPackex(PA_long32 selector, void *parameters, void **data, void *result);

// ---------------------------------------------------------------- registries

struct FakeHandle { std::vector<char> bytes; };
struct FakePicture { int refcount = 1; std::vector<char> bytes; };
struct FakeObject;
struct FakeCollection;
struct FakeValue {
    int kind = eVK_Undefined;
    double real = 0; PA_long32 longint = 0; char boolean = 0;
    std::u16string str;
    FakeObject *obj = nullptr; FakeCollection *col = nullptr; FakePicture *pic = nullptr;
};
struct FakeObject { std::map<std::u16string, FakeValue> props; };
struct FakeCollection { std::vector<FakeValue> items; };

static std::set<FakeHandle *> gHandles;
static std::set<PA_Unichar *> gStrings;
static std::set<FakePicture *> gPictures;
static std::set<FakeObject *> gObjects;
static std::set<FakeCollection *> gCollections;
static int gInvalidFrees = 0;
// how the synthetic engine implements CONVERT PICTURE (1002)
enum ConvertModel { kReplaceAndRelease, kFailUntouched, kNoopUntouched, kInPlace };
static ConvertModel gConvert = kReplaceAndRelease;

static std::u16string u16(const PA_Unistring &u) {
    return u.fString ? std::u16string((const char16_t *)u.fString, u.fLength) : std::u16string();
}

static void pic_release(FakePicture *p) {
    if (!p) return;
    if (!gPictures.count(p)) { gInvalidFrees++; return; }
    if (--p->refcount == 0) { gPictures.erase(p); delete p; }
}
static FakePicture *pic_new(const std::vector<char> &bytes) {
    FakePicture *p = new FakePicture; p->bytes = bytes; gPictures.insert(p); return p;
}
static void obj_free(FakeObject *o);
static void col_free(FakeCollection *c);
static void value_free(FakeValue &v) {
    if (v.obj) obj_free(v.obj);
    if (v.col) col_free(v.col);
    if (v.pic) pic_release(v.pic);
    v = FakeValue();
}
static void obj_free(FakeObject *o) {
    if (!gObjects.count(o)) { gInvalidFrees++; return; }
    gObjects.erase(o);
    for (auto &kv : o->props) value_free(kv.second);
    delete o;
}
static void col_free(FakeCollection *c) {
    if (!gCollections.count(c)) { gInvalidFrees++; return; }
    gCollections.erase(c);
    for (auto &v : c->items) value_free(v);
    delete c;
}
static FakeObject *obj_new() { FakeObject *o = new FakeObject; gObjects.insert(o); return o; }
static FakeCollection *col_new() { FakeCollection *c = new FakeCollection; gCollections.insert(c); return c; }
static FakeObject *obj_clone(FakeObject *o);
static FakeCollection *col_clone(FakeCollection *c);
static FakeValue value_clone(const FakeValue &v) {
    FakeValue r = v;
    if (v.obj) r.obj = obj_clone(v.obj);
    if (v.col) r.col = col_clone(v.col);
    if (v.pic) r.pic->refcount++;
    return r;
}
static FakeObject *obj_clone(FakeObject *o) {
    FakeObject *r = obj_new();
    for (auto &kv : o->props) r->props[kv.first] = value_clone(kv.second);
    return r;
}
static FakeCollection *col_clone(FakeCollection *c) {
    FakeCollection *r = col_new();
    for (auto &v : c->items) r->items.push_back(value_clone(v));
    return r;
}

static PA_Unistring str_new(const char16_t *s, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    memcpy(buf, s, len * sizeof(PA_Unichar)); buf[len] = 0;
    gStrings.insert(buf);
    PA_Unistring u; u.fLength = (PA_long32)len; u.fString = buf; u.fReserved1 = 0; u.fReserved2 = 0;
    return u;
}
static void str_free(PA_Unichar *s) {
    if (!s) return;
    if (!gStrings.count(s)) { gInvalidFrees++; }
    gStrings.erase(s);
    free(s); // ASan reports a double free if the plugin frees twice
}

// deep copy of a PA_Variable into engine storage (PA_SetObjectProperty semantics)
static FakeValue value_from_variable(const PA_Variable *v) {
    FakeValue r; r.kind = v->fType;
    switch (v->fType) {
        case eVK_Real: r.real = v->uValue.fReal; break;
        case eVK_Boolean: r.boolean = v->uValue.fBoolean; break;
        case eVK_Longint: r.longint = v->uValue.fLongint; break;
        case eVK_Unistring: r.str = u16(v->uValue.fString); break;
        case eVK_Object: if (v->uValue.fObject) r.obj = obj_clone((FakeObject *)v->uValue.fObject); break;
        case eVK_Collection: if (v->uValue.fCollection) r.col = col_clone((FakeCollection *)v->uValue.fCollection); break;
        case eVK_Picture: r.pic = (FakePicture *)v->uValue.fPicture; if (r.pic) r.pic->refcount++; break;
        default: break;
    }
    return r;
}
static void variable_from_value(const FakeValue &src, PA_Variable *v) {
    FakeValue c = value_clone(src);
    v->fType = (char)c.kind; v->fFiller = 0;
    switch (c.kind) {
        case eVK_Real: v->uValue.fReal = c.real; break;
        case eVK_Boolean: v->uValue.fBoolean = c.boolean; break;
        case eVK_Longint: v->uValue.fLongint = c.longint; break;
        case eVK_Unistring: v->uValue.fString = str_new(c.str.data(), c.str.size()); break;
        case eVK_Object: v->uValue.fObject = c.obj; break;
        case eVK_Collection: v->uValue.fCollection = c.col; break;
        case eVK_Picture: v->uValue.fPicture = c.pic; break;
        default: v->fType = eVK_Undefined; break;
    }
}
static void clear_variable(PA_Variable *v) {
    switch (v->fType) {
        case eVK_Unistring: str_free(v->uValue.fString.fString); break;
        case eVK_Picture: pic_release((FakePicture *)v->uValue.fPicture); break;
        case eVK_Object: if (v->uValue.fObject) obj_free((FakeObject *)v->uValue.fObject); break;
        case eVK_Collection: if (v->uValue.fCollection) col_free((FakeCollection *)v->uValue.fCollection); break;
        case eVK_Blob: {
            FakeHandle *h = (FakeHandle *)v->uValue.fBlob.fHandle;
            if (h) { if (!gHandles.count(h)) gInvalidFrees++; gHandles.erase(h); delete h; }
            break;
        }
        default: break;
    }
    v->fType = eVK_Undefined;
}

// a 2x2 24-bit BMP, the synthetic result of CONVERT PICTURE(...; ".bmp")
static std::vector<char> synthetic_bmp() {
    unsigned char b[] = {
        'B','M', 70,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 2,0,0,0, 2,0,0,0, 1,0, 24,0, 0,0,0,0, 16,0,0,0,
        0x13,0x0B,0,0, 0x13,0x0B,0,0, 0,0,0,0, 0,0,0,0,
        0,0,255, 255,255,255, 0,0, 255,0,0, 0,255,0, 0,0 };
    return std::vector<char>((char *)b, (char *)b + sizeof(b));
}

static void execute_command(EngineBlock *eb) {
    PA_Variable **params = (PA_Variable **)eb->fPtr1;
    PA_Variable *ret = (PA_Variable *)eb->fPtr2;
    switch ((int)eb->fParam1) {
        case 1471: ret->fType = eVK_Object; ret->uValue.fObject = obj_new(); break;
        case 1472: ret->fType = eVK_Collection; ret->uValue.fCollection = col_new(); break;
        case 1225: ret->fType = eVK_Object; ret->uValue.fObject = obj_clone((FakeObject *)params[0]->uValue.fObject); break;
        case 1002: { // CONVERT PICTURE: replaces the picture held by the variable
            PA_Variable *pv = params[0];
            if (gConvert == kFailUntouched) { eb->fError = -1; break; }
            if (gConvert == kNoopUntouched) break;
            if (gConvert == kInPlace) { ((FakePicture *)pv->uValue.fPicture)->bytes = synthetic_bmp(); break; }
            FakePicture *old = (FakePicture *)pv->uValue.fPicture;
            pv->uValue.fPicture = pic_new(synthetic_bmp());
            pic_release(old);
            break;
        }
        default: eb->fError = -2; break;
    }
}

static void call4d(short selector, EngineBlock *eb) {
    switch (selector) {
        case EX_GET_INFORMATION: eb->fParam2 = 0x00001A00; break;
        case EX_HANDLE_MANAGER: {
            FakeHandle *h = (FakeHandle *)eb->fHandle;
            switch ((int)eb->fParam1) {
                case 1: h = new FakeHandle; h->bytes.resize((size_t)eb->fParam2); gHandles.insert(h); eb->fHandle = (PA_Handle)h; break;
                case 2: if (!gHandles.count(h)) gInvalidFrees++; gHandles.erase(h); delete h; break;
                case 3: h->bytes.resize((size_t)eb->fParam2); break;
                case 4: eb->fParam2 = (sLONG_PTR)h->bytes.size(); break;
                case 5: eb->fParam3 = (sLONG_PTR)(h->bytes.empty() ? nullptr : h->bytes.data()); break;
                default: break;
            }
            break;
        }
        case EX_CREATE_UNISTRING: {
            const char16_t *src = (const char16_t *)eb->fHandle;
            size_t n = 0; while (src[n]) n++;
            eb->fUniString1 = str_new(src, n);
            break;
        }
        case EX_DISPOSE_UNISTRING: str_free(eb->fUniString1.fString); break;
        case EX_CREATE_PICTURE: {
            std::vector<char> b;
            if (eb->fPtr1 && eb->fParam1 > 0) b.assign((char *)eb->fPtr1, (char *)eb->fPtr1 + eb->fParam1);
            eb->fPicture = pic_new(b);
            break;
        }
        case EX_DISPOSE_PICTURE: pic_release((FakePicture *)eb->fPicture); break;
        case EX_DUPLICATE_PICTURE: {
            FakePicture *p = (FakePicture *)eb->fPicture;
            if (eb->fParam1 == 0) p->refcount++; else eb->fPicture = pic_new(p->bytes);
            break;
        }
        case EX_GET_PICTURE_DATA: {
            FakePicture *p = (FakePicture *)eb->fPicture;
            FakeHandle *h = (FakeHandle *)eb->fHandle;
            if (!gPictures.count(p) || eb->fParam1 != 1) { eb->fError = -3; break; }
            h->bytes = p->bytes;
            break;
        }
        case EX_CLEAR_VARIABLE: clear_variable((PA_Variable *)eb->fPtr1); break;
        case EX_EXECUTE_COMMAND_BY_ID: execute_command(eb); break;
        case EX_SET_OBJ_VALUE: {
            FakeValue v = value_from_variable((PA_Variable *)eb->fPtr2);
            if (eb->fShort == eVK_Collection) {
                FakeCollection *c = (FakeCollection *)eb->fPtr1;
                if (eb->fLongint >= (PA_long32)c->items.size()) c->items.resize(eb->fLongint + 1);
                value_free(c->items[eb->fLongint]);
                c->items[eb->fLongint] = v;
            } else {
                FakeObject *o = (FakeObject *)eb->fPtr1;
                std::u16string k = u16(eb->fUniString1);
                value_free(o->props[k]);
                o->props[k] = v;
            }
            break;
        }
        case EX_GET_OBJ_VALUE: {
            PA_Variable *out = (PA_Variable *)eb->fPtr2;
            out->fType = eVK_Undefined;
            if (eb->fShort == eVK_Collection) {
                FakeCollection *c = (FakeCollection *)eb->fPtr1;
                if (eb->fLongint == -1) { out->fType = eVK_Longint; out->uValue.fLongint = (PA_long32)c->items.size(); }
                else if (eb->fLongint < (PA_long32)c->items.size()) variable_from_value(c->items[eb->fLongint], out);
            } else {
                FakeObject *o = (FakeObject *)eb->fPtr1;
                auto it = o->props.find(u16(eb->fUniString1));
                if (it != o->props.end()) variable_from_value(it->second, out);
            }
            break;
        }
        default:
            fprintf(stderr, "unexpected Call4D selector %d\n", selector);
            abort();
    }
}

// ---------------------------------------------------------------- ZXing stub

static ZXing::Results gStubResults;
static int gLastWidth = 0, gLastHeight = 0;
ZXing::Results ZXing::ReadBarcodes(const ImageView &image, const DecodeHints &) {
    gLastWidth = image._width; gLastHeight = image._height;
    return gStubResults;
}

// ---------------------------------------------------------------- tests

struct Snapshot { size_t handles, strings, pictures, objects, collections; int invalid; };
static Snapshot snap() {
    return { gHandles.size(), gStrings.size(), gPictures.size(), gObjects.size(), gCollections.size(), gInvalidFrees };
}
static bool same(const Snapshot &a, const Snapshot &b, const char *name) {
    bool ok = a.handles == b.handles && a.strings == b.strings && a.pictures == b.pictures &&
              a.objects == b.objects && a.collections == b.collections && a.invalid == b.invalid;
    if (!ok)
        fprintf(stderr, "  [%s] leaked/invalid: handles %+ld strings %+ld pictures %+ld objects %+ld collections %+ld invalidFrees %+d\n",
                name, (long)(b.handles - a.handles), (long)(b.strings - a.strings), (long)(b.pictures - a.pictures),
                (long)(b.objects - a.objects), (long)(b.collections - a.collections), b.invalid - a.invalid);
    return ok;
}

static int gFailures = 0;
static void run(const char *name, const std::function<bool()> &fn) {
    bool ok = fn();
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) gFailures++;
}

static PA_ObjectRef call_decode(PA_Picture input) {
    PA_Picture *slot = &input;
    void *params[1] = { slot };
    PA_ObjectRef result = nullptr;
    FourDPackex(1, params, nullptr, &result);
    return result;
}

static ZXing::Result sample_result() {
    ZXing::Result r;
    r._text = L"HELLO"; r._ecLevel = L"M"; r._symbologyIdentifier = "]Q1"; r._sequenceId = "seq";
    for (int i = 0; i < 4; i++) { r._position[i].x = i; r._position[i].y = 10 * i; }
    return r;
}

static void clear_object(PA_ObjectRef o) {
    PA_Variable v; PA_SetObjectVariable(&v, o); PA_ClearVariable(&v);
}

int main() {
    PackInitBlock init = {};
    init.fCall4Dex = call4d;
    FourDPackex(kInitPlugin, &init, nullptr, nullptr);

    std::vector<char> png = {'n','o','t','-','b','m','p'};

    // ---- defect tests (pictureToImage)
    run("defect_decode_picture_variable_no_leak", [&] {
        gStubResults.clear();
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        Snapshot a = snap();
        PA_ObjectRef r = call_decode(in);
        clear_object(r);
        Snapshot b = snap();
        bool callerIntact = gPictures.count((FakePicture *)in) && ((FakePicture *)in)->refcount == 1;
        PA_DisposePicture(in);
        return same(a, b, "decode/no results") && callerIntact;
    });
    run("defect_decode_convert_failure_releases_retained_param", [&] {
        gStubResults.clear(); gConvert = kFailUntouched;
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        Snapshot a = snap();
        PA_ObjectRef r = call_decode(in);
        clear_object(r);
        Snapshot b = snap();
        gConvert = kReplaceAndRelease;
        bool refOk = ((FakePicture *)in)->refcount == 1;
        if (!refOk) fprintf(stderr, "  caller picture refcount %d (expected 1)\n", ((FakePicture *)in)->refcount);
        PA_DisposePicture(in);
        return same(a, b, "decode/convert failure") && refOk;
    });
    run("defect_decode_with_result_no_leak", [&] {
        gStubResults = { sample_result() };
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        Snapshot a = snap();
        PA_ObjectRef r = call_decode(in);
        clear_object(r);
        Snapshot b = snap();
        PA_DisposePicture(in);
        return same(a, b, "decode/one result");
    });

    // ---- defect tests (JSON helpers)
    auto helper = [&](const char *name, const std::function<void(PA_ObjectRef)> &set) {
        Snapshot a = snap();
        PA_ObjectRef o = PA_CreateObject();
        set(o);
        clear_object(o);
        return same(a, snap(), name);
    };
    run("defect_ob_set_s_wchar_key_no_leak", [&] { return helper("ob_set_s(wchar)", [](PA_ObjectRef o) { ob_set_s(o, L"k", "v"); }); });
    run("defect_ob_set_a_wchar_no_leak", [&] { return helper("ob_set_a(wchar)", [](PA_ObjectRef o) { ob_set_a(o, L"k", L"v"); }); });
    run("defect_ob_set_s_char_key_no_leak", [&] { return helper("ob_set_s(char)", [](PA_ObjectRef o) { ob_set_s(o, "k", "v"); }); });
    run("defect_ob_set_a_cutf16_no_leak", [&] {
        return helper("ob_set_a(CUTF16String)", [](PA_ObjectRef o) { CUTF16String s((const PA_Unichar *)u"v"); ob_set_a(o, L"k", &s); });
    });
    run("defect_ob_set_p_no_leak", [&] {
        // like ob_set_c/ob_set_o, ob_set_p consumes the reference it is given
        return helper("ob_set_p", [&](PA_ObjectRef o) {
            ob_set_p(o, L"k", PA_CreatePicture(png.data(), (PA_long32)png.size()));
        });
    });

    // ---- controls
    run("control_decode_output_contents", [&] {
        gStubResults = { sample_result() };
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        PA_ObjectRef r = call_decode(in);
        FakeObject *status = (FakeObject *)r;
        bool ok = status && status->props.count(u"results");
        if (ok) {
            FakeCollection *col = status->props[u"results"].col;
            ok = col && col->items.size() == 1;
            if (ok) {
                FakeObject *o = col->items[0].obj;
                ok = o->props[u"text"].str == u"HELLO" && o->props[u"status"].str == u"noError" &&
                     o->props[u"format"].str == u"QRCode" && o->props[u"ecLevel"].str == u"M" &&
                     o->props[u"corners"].col && o->props[u"corners"].col->items.size() == 4 &&
                     o->props[u"corners"].col->items[3].obj->props[u"y"].real == 30;
            }
        }
        ok = ok && gLastWidth == 2 && gLastHeight == 2;
        clear_object(r);
        PA_DisposePicture(in);
        return ok;
    });
    run("control_scalar_and_container_helpers_no_leak", [&] {
        return helper("scalars/containers", [](PA_ObjectRef o) {
            ob_set_n(o, L"n", 1.5); ob_set_b(o, L"b", true); ob_set_i(o, L"i", 3); ob_set_0(o, L"z");
            ob_set_c(o, L"c", PA_CreateCollection()); ob_set_o(o, L"o", PA_CreateObject());
        });
    });
    auto engine_model = [&](const char *name, ConvertModel model, bool expectDecoded) {
        gStubResults.clear(); gConvert = model; gLastWidth = gLastHeight = 0;
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        Snapshot a = snap();
        PA_ObjectRef r = call_decode(in);
        clear_object(r);
        Snapshot b = snap();
        gConvert = kReplaceAndRelease;
        bool alive = gPictures.count((FakePicture *)in) && ((FakePicture *)in)->refcount == 1;
        if (!alive) fprintf(stderr, "  [%s] caller picture freed or over-retained\n", name);
        bool decoded = (gLastWidth == 2) == expectDecoded;
        if (alive) PA_DisposePicture(in);
        return same(a, b, name) && alive && decoded;
    };
    run("control_engine_noop_untouched_caller_picture_live", [&] {
        return engine_model("engine no-op", kNoopUntouched, false);
    });
    run("control_engine_inplace_caller_picture_live", [&] {
        return engine_model("engine in-place", kInPlace, true);
    });
    run("control_engine_replace_caller_picture_live", [&] {
        return engine_model("engine replace", kReplaceAndRelease, true);
    });
    run("control_caller_picture_not_freed", [&] {
        gStubResults.clear();
        PA_Picture in = PA_CreatePicture(png.data(), (PA_long32)png.size());
        PA_ObjectRef r = call_decode(in);
        clear_object(r);
        bool alive = gPictures.count((FakePicture *)in) && ((FakePicture *)in)->refcount >= 1;
        PA_DisposePicture(in);
        return alive;
    });

    printf("%d failure(s), %d invalid free(s)\n", gFailures, gInvalidFrees);
    return gFailures ? 1 : 0;
}
