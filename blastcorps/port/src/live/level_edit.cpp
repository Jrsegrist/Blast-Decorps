/* bc.exe's level editor, its model (see level_edit.h). */
#include "level_edit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace live {
namespace edit {

namespace {

LmChange blank(int op, int kind, int index) {
    LmChange c;
    memset(&c, 0, sizeof c);
    c.op = op, c.kind = kind, c.index = index;
    return c;
}

void addFields(LmChange &c, const std::vector<FieldValue> &f) {
    for (const FieldValue &v : f)
        if (c.nfields < LM_MAX_FIELDS) c.field[c.nfields] = v.field, c.value[c.nfields++] = v.value;
}

/* the marks of one change (the kind's array of 0 / 1 / 2, as the records' order moves) */
void markChange(std::vector<uint8_t> &m, const LmChange &c, uint8_t how) {
    if (c.op == LM_OP_SET && c.index >= 0 && size_t(c.index) < m.size()) {
        if (m[c.index] != 1) m[c.index] = how;
    } else if (c.op == LM_OP_ADD && c.index >= 0 && size_t(c.index) <= m.size()) {
        m.insert(m.begin() + c.index, how);
    } else if (c.op == LM_OP_DELETE && c.index >= 0 && size_t(c.index) < m.size()) {
        m.erase(m.begin() + c.index);
    }
}

bool invert(const double a[4][4], double out[4][4]) {
    double m[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++) m[r][c] = c < 4 ? a[r][c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
        if (std::fabs(m[p][c]) < 1e-12) return false;
        if (p != c)
            for (int k = 0; k < 8; k++) std::swap(m[p][k], m[c][k]);
        const double d = m[c][c];
        for (int k = 0; k < 8; k++) m[c][k] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            const double f = m[r][c];
            for (int k = 0; k < 8; k++) m[r][k] -= f * m[c][k];
        }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[r][c] = m[r][4 + c];
    return true;
}

void mul(const double a[4][4], const double b[4][4], double out[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += a[r][k] * b[k][c];
            out[r][c] = s;
        }
}

}  // namespace

LmChange makeSet(int kind, int index, const std::vector<FieldValue> &fields) {
    LmChange c = blank(LM_OP_SET, kind, index);
    addFields(c, fields);
    return c;
}

LmChange makeAdd(int kind, int index, int copyOf, const std::vector<FieldValue> &fields) {
    LmChange c = blank(LM_OP_ADD, kind, index);
    c.copy_of = copyOf;
    addFields(c, fields);
    return c;
}

LmChange makeDelete(int kind, int index) {
    return blank(LM_OP_DELETE, kind, index);
}

std::string describe(const LmChange &c) {
    char b[512];
    lm_describe(&c, b, sizeof b);
    return b;
}

std::string describe(const EditStep &s) {
    switch (s.type) {
    case STEP_RECORD: return describe(s.rec);
    case STEP_RECORDS: return s.label.empty() ? std::to_string(s.recs.size()) + " record changes" : s.label;
    case STEP_TERRAIN: return s.label.empty() ? "terrain: " + s.ter.what : s.label;
    case STEP_TEXTURE: return s.label.empty() ? "texture " + std::to_string(s.tex.id) + ": an image" : s.label;
    default: return "?";
    }
}

Editor::~Editor() {
    close();
}

void Editor::close() {
    lm_free(lv_);
    lv_ = nullptr;
    level_ = -1;
    orig_.clear();
    base_.clear(), changes_.clear(), steps_.clear(), redo_.clear();
    image_.clear();
    for (auto &m : marks_) m.clear();
    baseName_.clear();
    applied_.clear();
    if (baseGeo_.valid) lg_edit_free(&baseGeo_.e);
    baseGeo_ = BaseGeo();
    baseTex_.clear();
    model_.close();
    groundRef_.clear();
    for (int k = 0; k < 4; k++) listN[k] = listCap[k] = 0;
}

bool Editor::open(int level, const uint8_t *orig, size_t size, const std::vector<LmChange> &base,
                  const std::string &baseName, const LgEdit *baseGeo, const std::vector<TexSlot> &baseTex) {
    close();
    orig_.assign(orig, orig + size);
    base_ = base;
    baseName_ = baseName;
    baseTex_ = baseTex;
    level_ = level;
    if (baseGeo != nullptr && !lg_edit_empty(baseGeo)) {
        if (lm_geo_copy(&baseGeo_.e, baseGeo)) {
            error_ = "out of memory";
            close();
            return false;
        }
        baseGeo_.valid = true;
    }
    {
        int cap[4];
        char e[300];
        if (lg_budgets(orig, size, level, listOwn, cap, e, sizeof e))
            for (int k = 0; k < 4; k++) listOwn[k] = 0;
    }
    std::string terr;
    if (!model_.open(orig, size, terr)) {
        /* (the record tools still work; a mod's geometry is kept as it is) */
        model_.close();
    }
    if (!rebuild()) {
        const std::string e = error_;
        close();
        error_ = e;
        return false;
    }
    applied_ = imageSha1();
    return true;
}

bool Editor::rebuild() {
    char err[512];
    const auto t0 = std::chrono::steady_clock::now();
    LmLevel *lv = lm_open(orig_.data(), orig_.size(), err, sizeof err);
    if (lv == nullptr) {
        error_ = err;
        return false;
    }
    std::vector<uint8_t> marks[LM_KINDS];
    for (int k = 0; k < LM_KINDS; k++) marks[k].assign(size_t(lm_count(lv, k)), 0);
    for (size_t i = 0; i < base_.size(); i++) {
        if (lm_apply(lv, &base_[i], err, sizeof err)) {
            error_ = "the mod's change " + std::to_string(i) + " (" + describe(base_[i]) + "): " + err;
            lm_free(lv);
            return false;
        }
        markChange(marks[base_[i].kind], base_[i], 2);
    }
    if (model_.isOpen()) {
        model_.reset();
        std::string e;
        if (baseGeo_.valid && !model_.import(baseGeo_.e, e)) {
            error_ = "the mod's geometry: " + e;
            lm_free(lv);
            return false;
        }
    }
    auto snapshot = [&]() {
        groundRef_.clear();
        for (const terrain::Pos &p : model_.positions()) groundRef_.push_back(p.cur);
    };
    snapshot();
    changes_.clear();
    for (size_t i = 0; i < steps_.size(); i++) {
        const EditStep &s = steps_[i];
        std::vector<const LmChange *> recs;
        if (s.type == STEP_RECORD) recs.push_back(&s.rec);
        if (s.type == STEP_RECORDS) {
            snapshot();
            for (const LmChange &c : s.recs) recs.push_back(&c);
        }
        for (const LmChange *c : recs) {
            if (lm_apply(lv, c, err, sizeof err)) {
                error_ = "change " + std::to_string(i) + " (" + describe(*c) + "): " + err;
                lm_free(lv);
                return false;
            }
            markChange(marks[c->kind], *c, 1);
            changes_.push_back(*c);
        }
        if (s.type == STEP_TERRAIN) {
            std::string e;
            if (!model_.isOpen()) {
                error_ = "the terrain tools don't work on this level (its display lists didn't parse)";
                lm_free(lv);
                return false;
            }
            if (!model_.apply(s.ter, e)) {
                error_ = describe(s) + ": " + e;
                lm_free(lv);
                return false;
            }
        }
    }
    if (lm_check(lv, err, sizeof err)) {
        error_ = err;
        lm_free(lv);
        return false;
    }
    size_t n = 0;
    uint8_t *img = lm_build(lv, &n, nullptr);
    if (img == nullptr) {
        error_ = "out of memory";
        lm_free(lv);
        return false;
    }
    LgEdit ge;
    if (!geometry(ge)) {
        free(img);
        lm_free(lv);
        error_ = "out of memory";
        return false;
    }
    if (!lg_edit_empty(&ge)) {
        uint8_t *g = nullptr;
        size_t gn = 0;
        char e[400];
        const int r = lg_apply(img, n, orig_.data(), orig_.size(), &ge, level_, &g, &gn, e, sizeof e);
        free(img);
        if (r) {
            lg_edit_free(&ge);
            lm_free(lv);
            error_ = std::string("the geometry: ") + e;
            return false;
        }
        img = g, n = gn;
    }
    lg_edit_free(&ge);
    if (n > orig_.size() + kMaxGrowth) {
        char b[200];
        snprintf(b, sizeof b, "the level would be %u bytes, %u more than the ROM's: the game's room for a level "
                 "allows %u more (port_mods.c)", unsigned(n), unsigned(n - orig_.size()), unsigned(kMaxGrowth));
        error_ = b;
        free(img);
        lm_free(lv);
        return false;
    }
    if (lg_budgets(img, n, level_, listN, listCap, err, sizeof err))
        for (int k = 0; k < 4; k++) listN[k] = listCap[k] = 0;
    lm_free(lv_);
    lv_ = lv;
    image_.assign(img, img + n);
    free(img);
    for (int k = 0; k < LM_KINDS; k++) marks_[k] = marks[k];
    buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

bool Editor::geometry(LgEdit &out) const {
    if (model_.isOpen()) return model_.exportEdit(out);
    if (baseGeo_.valid) return lm_geo_copy(&out, &baseGeo_.e) == 0;
    memset(&out, 0, sizeof out);
    return true;
}

std::vector<TexSlot> Editor::textures() const {
    std::vector<TexSlot> out = baseTex_;
    for (const EditStep &s : steps_) {
        if (s.type != STEP_TEXTURE) continue;
        bool done = false;
        for (TexSlot &t : out)
            if (t.id == s.tex.id) t = s.tex, done = true;
        if (!done) out.push_back(s.tex);
    }
    return out;
}

std::vector<LmChange> Editor::followGround() const {
    std::vector<LmChange> out;
    if (!model_.isOpen() || lv_ == nullptr) return out;
    std::vector<terrain::P3> before = groundRef_;
    const std::vector<terrain::Pos> &ps = model_.positions();
    for (size_t i = before.size(); i < ps.size(); i++) before.push_back(ps[i].orig);
    static const int kinds[] = {LM_VEHICLE, LM_OBJECT, LM_RDU, LM_TNT, LM_CRATE, LM_COMMPT, LM_BLOCK, LM_SLOT};
    for (int kind : kinds) {
        const int fx = lm_field_index(kind, "x"), fy = lm_field_index(kind, "y"), fz = lm_field_index(kind, "z");
        if (fx < 0 || fy < 0 || fz < 0) continue;
        for (int i = 0; i < lm_count(lv_, kind); i++) {
            long x, y, z;
            double h0, h1;
            if (lm_get(lv_, kind, i, fx, &x) || lm_get(lv_, kind, i, fy, &y) || lm_get(lv_, kind, i, fz, &z)) continue;
            /* (objects hold unsigned coordinates: the s16 they stand for) */
            const double px = double(int16_t(uint16_t(x))), pz = double(int16_t(uint16_t(z)));
            if (!model_.heightAtWith(before, px, pz, h0) || !model_.heightAt(px, pz, h1)) continue;
            const long d = std::lround(h1 - h0);
            if (d == 0) continue;
            long lo, hi;
            lm_field_range(kind, fy, &lo, &hi);
            long ny = y + d;
            if (kind == LM_OBJECT) ny = long(uint16_t(int16_t(int16_t(uint16_t(y)) + d)));
            ny = std::max(lo, std::min(hi, ny));
            if (ny != y) out.push_back(makeSet(kind, i, {{fy, ny}}));
        }
    }
    return out;
}

bool Editor::push(const LmChange &c) {
    EditStep s;
    s.type = STEP_RECORD;
    s.rec = c;
    return pushStep(s);
}

bool Editor::pushStep(const EditStep &s) {
    if (!isOpen()) return false;
    steps_.push_back(s);
    if (!rebuild()) {
        steps_.pop_back();
        const std::string e = error_;
        rebuild();
        error_ = e;
        return false;
    }
    redo_.clear();
    error_.clear();
    return true;
}

bool Editor::undo() {
    if (steps_.empty()) return false;
    redo_.push_back(steps_.back());
    steps_.pop_back();
    return rebuild();
}

bool Editor::redo() {
    if (redo_.empty()) return false;
    steps_.push_back(redo_.back());
    redo_.pop_back();
    if (!rebuild()) {
        redo_.push_back(steps_.back());
        steps_.pop_back();
        rebuild();
        return false;
    }
    return true;
}

void Editor::revertAll() {
    while (!steps_.empty()) {
        redo_.push_back(steps_.back());
        steps_.pop_back();
    }
    rebuild();
}

int Editor::count(int kind) const {
    return lv_ ? lm_count(lv_, kind) : 0;
}

bool Editor::get(int kind, int index, int field, long &v) const {
    return lv_ && lm_get(lv_, kind, index, field, &v) == 0;
}

long Editor::get(int kind, int index, const char *field, long fallback) const {
    long v;
    return get(kind, index, lm_field_index(kind, field), v) ? v : fallback;
}

int Editor::edited(int kind, int index) const {
    if (kind < 0 || kind >= LM_KINDS || index < 0 || size_t(index) >= marks_[kind].size()) return 0;
    return marks_[kind][index];
}

std::string Editor::imageSha1() const {
    char h[41];
    lm_sha1_hex(image_.data(), image_.size(), h);
    return h;
}

std::string Editor::originalSha1() const {
    char h[41];
    lm_sha1_hex(orig_.data(), orig_.size(), h);
    return h;
}

std::string Editor::modText(const std::string &name, const std::string &description) const {
    LmMod m;
    memset(&m, 0, sizeof m);
    snprintf(m.name, sizeof m.name, "%s", name.c_str());
    snprintf(m.description, sizeof m.description, "%s", description.c_str());
    LmModLevel ml;
    memset(&ml, 0, sizeof ml);
    ml.level = level_;
    snprintf(ml.original_sha1, sizeof ml.original_sha1, "%s", originalSha1().c_str());
    snprintf(ml.edited_sha1, sizeof ml.edited_sha1, "%s", imageSha1().c_str());
    std::vector<LmChange> all = base_;
    all.insert(all.end(), changes_.begin(), changes_.end());
    ml.nchanges = int(all.size());
    ml.changes = all.data();
    LgEdit ge;
    if (!geometry(ge)) return "";
    ml.geo = lg_edit_empty(&ge) ? nullptr : &ge;
    m.nlevels = 1;
    m.levels = &ml;
    const std::vector<TexSlot> tex = textures();
    std::vector<LmTexture> lt(tex.size());
    for (size_t i = 0; i < tex.size(); i++) {
        memset(&lt[i], 0, sizeof lt[i]);
        lt[i].id = tex[i].id, lt[i].fmt = tex[i].fmt, lt[i].siz = tex[i].siz, lt[i].isNew = tex[i].isNew;
        lt[i].from = tex[i].from, lt[i].mips = tex[i].mips;
        lt[i].hasPalette = tex[i].palette >= 0, lt[i].palette = tex[i].palette >= 0 ? tex[i].palette : 0;
        snprintf(lt[i].slot_sha1, sizeof lt[i].slot_sha1, "%s", tex[i].sha1.c_str());
        lt[i].png = const_cast<char *>(tex[i].png.c_str());
    }
    m.ntextures = int(lt.size());
    m.textures = lt.empty() ? nullptr : lt.data();
    char *t = lm_mod_write(&m);
    std::string s = t ? t : "";
    free(t);
    lg_edit_free(&ge);
    return s;
}

/* ---- the gizmo's math ---- */
bool pickRay(const double view[4][4], const double proj[4][4], double rx, double ry, double rw, double rh, double sx,
             double sy, V3 &origin, V3 &dir) {
    double vp[4][4], inv[4][4], vinv[4][4];
    mul(view, proj, vp);
    if (rw <= 0 || rh <= 0 || !invert(vp, inv) || !invert(view, vinv)) return false;
    const double nx = (sx - rx) / rw * 2.0 - 1.0, ny = 1.0 - (sy - ry) / rh * 2.0;
    const double in[4] = {nx, ny, 0.5, 1.0};
    double p[4];
    for (int j = 0; j < 4; j++) p[j] = in[0] * inv[0][j] + in[1] * inv[1][j] + in[2] * inv[2][j] + in[3] * inv[3][j];
    if (std::fabs(p[3]) < 1e-12) return false;
    const V3 eye = {vinv[3][0], vinv[3][1], vinv[3][2]};
    V3 d = {p[0] / p[3] - eye.x, p[1] / p[3] - eye.y, p[2] / p[3] - eye.z};
    const double n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!(n > 1e-9)) return false;
    d.x /= n, d.y /= n, d.z /= n;
    /* away from the camera: it looks along view space's -z */
    const V3 fwd = {-vinv[2][0], -vinv[2][1], -vinv[2][2]};
    if (d.x * fwd.x + d.y * fwd.y + d.z * fwd.z < 0) d.x = -d.x, d.y = -d.y, d.z = -d.z;
    origin = eye;
    dir = d;
    return true;
}

bool rayPlaneY(const V3 &o, const V3 &d, double y, double &x, double &z) {
    if (std::fabs(d.y) < 1e-9) return false;
    const double t = (y - o.y) / d.y;
    if (t <= 0) return false;
    x = o.x + d.x * t;
    z = o.z + d.z * t;
    return true;
}

bool rayAxis(const V3 &o, const V3 &d, const V3 &p, const V3 &a, double &t) {
    const V3 w = {p.x - o.x, p.y - o.y, p.z - o.z};
    const double b = a.x * d.x + a.y * d.y + a.z * d.z;
    const double den = 1.0 - b * b;
    if (std::fabs(den) < 1e-6) return false;
    const double dw = d.x * w.x + d.y * w.y + d.z * w.z, aw = a.x * w.x + a.y * w.y + a.z * w.z;
    t = (b * dw - aw) / den;
    return std::isfinite(t);
}

int headingFromDegrees(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d < 0) d += 360.0;
    return int(std::lround(d * 4096.0 / 360.0)) & 0xFFF;
}

double degreesFromHeading(long h) {
    return double(h & 0xFFF) * 360.0 / 4096.0;
}

long snapTo(double v, double step) {
    if (!(step > 0)) return std::lround(v);
    return std::lround(std::round(v / step) * step);
}

}  // namespace edit
}  // namespace live
