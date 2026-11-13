// MDLXImporter - IK Rig (see ik_rig.h for what it builds and why it stays exact)
//
// TRAPS (measured, keep them in mind when changing this file)
//   * A bone's local rotation is taken from nodeTM * inverse(parentTM) by polar
//     decomposition: exact for any scale, also a non-uniform one with its own
//     axis (MaxScript's rotation_list.value evaluates the Orientation
//     Constraint without the real parent and is wrong).
//   * Linear rotation keys: the value convention is read from a real
//     controller once (rotationConjugate), like the importer's
//     readMaxKeyConvention. Keys are written in time order on emptied
//     controllers (inserted between two others, MaxScript moved every later key).
//   * An IK chain made straight (a T-pose) has no bending direction: its solver
//     flips between two solutions depending on the order frames are evaluated
//     in. The rig is therefore set up in the most bent frame among those that
//     bend the usual way (one frame bent backwards turned every other by 180).
//   * Weapons: only nodes whose name says weapon. By motion alone a cape or a pad
//     beside a resting arm looked held as well, and Link Constraints on such
//     nodes evaluated differently depending on the frame order.

#include "ik_rig.h"

#include <istdplug.h>
#include <modstack.h>
#include <decomp.h>
#include <hold.h>
#include <maxscript/maxscript.h>
#include <wdx_localization.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace wdx {
namespace ikrig {

namespace {

// ─── Strings ────────────────────────────────────────────────────────────────

std::wstring tr(const char* key, const wchar_t* fallback) { return wdx::l10n::TrOr(key, fallback); }

std::wstring fmt(std::wstring s, const std::vector<std::wstring>& args)
{
    for (size_t i = args.size(); i-- > 0;) {   // %10 before %1
        const std::wstring token = L"%" + std::to_wstring(i + 1);
        for (size_t p = s.find(token); p != std::wstring::npos; p = s.find(token, p + args[i].size()))
            s.replace(p, token.size(), args[i]);
    }
    return s;
}

std::wstring num(double v, int decimals)
{
    std::wostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

// A value as a MaxScript literal, full float precision.
std::wstring mxs(float v)
{
    std::wostringstream ss;
    ss << std::setprecision(9) << v;
    return ss.str();
}
std::wstring mxs(const Point3& p) { return L"[" + mxs(p.x) + L"," + mxs(p.y) + L"," + mxs(p.z) + L"]"; }
std::wstring mxs(const Matrix3& m)
{
    return L"(matrix3 " + mxs(m.GetRow(0)) + L" " + mxs(m.GetRow(1)) + L" " + mxs(m.GetRow(2)) + L" " +
           mxs(m.GetRow(3)) + L")";
}
std::wstring mxsString(const wchar_t* s)
{
    std::wstring out = L"\"";
    for (; *s; ++s) {
        if (*s == L'"' || *s == L'\\') out += L'\\';
        out += *s;
    }
    return out + L"\"";
}

// Runs a script; its string result (empty on failure).
std::wstring runScript(const std::wstring& script) { return wdx::l10n::detail::EvalString(script); }

std::vector<ULONG> parseHandles(const std::wstring& s)
{
    std::vector<ULONG> out;
    std::wstringstream ss(s);
    std::wstring item;
    while (std::getline(ss, item, L','))
        if (!item.empty()) out.push_back(static_cast<ULONG>(std::wcstoul(item.c_str(), nullptr, 10)));
    return out;
}

// ─── Matrices ───────────────────────────────────────────────────────────────

Matrix3 identity3()
{
    Matrix3 m;
    m.IdentityMatrix();
    return m;
}

Matrix3 unitRot(Matrix3 m)
{
    m.NoTrans();
    for (int r = 0; r < 3; ++r) {
        const Point3 row = m.GetRow(r);
        const float len = Length(row);
        if (len > 1e-12f) m.SetRow(r, row / len);
    }
    return m;
}

Matrix3 transposed3(const Matrix3& m)
{
    Matrix3 t = identity3();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            Point3 row = t.GetRow(c);
            row[r] = m.GetRow(r)[c];
            t.SetRow(c, row);
        }
    return t;
}

// Rotation part of S*R by polar decomposition; stops once converged (an
// unscaled bone needs one or two steps).
Matrix3 polar(Matrix3 m)
{
    m.NoTrans();
    for (int k = 0; k < 30; ++k) {
        const Matrix3 it = transposed3(Inverse(m));
        Matrix3 n = identity3();
        for (int r = 0; r < 3; ++r) n.SetRow(r, (m.GetRow(r) + it.GetRow(r)) * 0.5f);
        const float moved = Length(n.GetRow(0) - m.GetRow(0)) + Length(n.GetRow(1) - m.GetRow(1)) +
                            Length(n.GetRow(2) - m.GetRow(2));
        m = n;
        if (moved < 1e-7f) break;
    }
    return m;
}

INode* realParent(INode* n)
{
    INode* p = n ? n->GetParentNode() : nullptr;
    return (p && !p->IsRootNode()) ? p : nullptr;
}

// The local rotation Max really evaluates for the node.
Matrix3 localRot(INode* n, TimeValue t)
{
    Matrix3 w = n->GetNodeTM(t);
    if (INode* p = realParent(n)) w = w * Inverse(p->GetNodeTM(t));
    return polar(w);
}

double angleBetween(const Matrix3& a, const Matrix3& b)
{
    Quat q(unitRot(a * Inverse(b)));
    q.Normalize();
    const double w = std::min(1.0, std::fabs(static_cast<double>(q.w)));
    return 2.0 * std::acos(w) * 180.0 / 3.14159265358979323846;
}

double rowDiff(const Matrix3& a, const Matrix3& b)
{
    double d = 0.0;
    for (int r = 0; r < 3; ++r) d = std::max(d, static_cast<double>(Length(a.GetRow(r) - b.GetRow(r))));
    return d;
}

bool isAncestor(INode* a, INode* b)
{
    for (INode* p = b ? b->GetParentNode() : nullptr; p && !p->IsRootNode(); p = p->GetParentNode())
        if (p == a) return true;
    return false;
}

// ─── Frames ─────────────────────────────────────────────────────────────────

struct Frames {
    int first = 0, count = 1;
    TimeValue tpf = 160;
    TimeValue at(int i) const { return (first + i) * tpf; }
};

Frames sceneFrames()
{
    Frames f;
    f.tpf = std::max<TimeValue>(GetTicksPerFrame(), 1);
    const Interval r = GetCOREInterface()->GetAnimRange();
    f.first = r.Start() / f.tpf;
    f.count = std::max(1, r.End() / f.tpf - f.first + 1);
    return f;
}

// ─── Rotation keys ──────────────────────────────────────────────────────────

Quat conjugated(const Quat& q) { return Quat(-q.x, -q.y, -q.z, q.w); }

Matrix3 quatMatrix(const Quat& q)
{
    Matrix3 m;
    q.MakeMatrix(m);
    m.NoTrans();
    return m;
}

// How a Linear Rotation key's value turns into a matrix, read from a real
// controller once (see the importer's readMaxKeyConvention).
bool rotationConjugate()
{
    static int known = -1;
    if (known >= 0) return known == 1;
    known = 0;
    Quat test(0.2f, -0.4f, 0.3f, 0.84f);
    test.Normalize();
    Control* rot = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    IKeyControl* rk = rot ? GetKeyControlInterface(rot) : nullptr;
    if (rk) {
        ILinRotKey k;
        memset(&k, 0, sizeof(k));
        k.val = test;
        rk->AppendKey(&k);
        Matrix3 m = identity3();
        Interval iv = FOREVER;
        rot->GetValue(0, &m, iv, CTRL_RELATIVE);
        m.NoTrans();
        if (rowDiff(m, quatMatrix(conjugated(test))) < 1e-4) known = 1;
    }
    if (rot) rot->MaybeAutoDelete();
    return known == 1;
}

Quat keyFor(const Matrix3& r)
{
    Quat q(unitRot(r));
    q.Normalize();
    return rotationConjugate() ? conjugated(q) : q;
}

// The rotation a rotation controller holds at t, as a matrix.
Matrix3 rotationAt(Control* c, TimeValue t)
{
    Matrix3 m = identity3();
    Interval iv = FOREVER;
    c->GetValue(t, &m, iv, CTRL_RELATIVE);
    m.NoTrans();
    return m;
}

Point3 positionAt(Control* c, TimeValue t)
{
    Point3 p(0, 0, 0);
    Interval iv = FOREVER;
    c->GetValue(t, &p, iv, CTRL_ABSOLUTE);
    return p;
}

void changed(Control* c) { c->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE); }

void clearKeys(Control* c)
{
    if (IKeyControl* k = c ? GetKeyControlInterface(c) : nullptr) {
        k->SetNumKeys(0);
        changed(c);
    }
}

// While a limb is baked frame by frame, a control holds one constant value (a
// single key) that is replaced for each frame: a key list growing by one key
// per frame, sorted after every key, made Hidan's 6133 frames take a minute.
void setConstRot(Control* c, const Matrix3& r)
{
    IKeyControl* k = GetKeyControlInterface(c);
    if (!k) return;
    k->SetNumKeys(0);
    ILinRotKey key;
    memset(&key, 0, sizeof(key));
    key.val = keyFor(r);
    k->AppendKey(&key);
    changed(c);
}

void setConstPos(Control* c, const Point3& p)
{
    IKeyControl* k = GetKeyControlInterface(c);
    if (!k) return;
    k->SetNumKeys(0);
    ILinPoint3Key key;
    memset(&key, 0, sizeof(key));
    key.val = p;
    k->AppendKey(&key);
    changed(c);
}

// The chosen keys, written once in time order. Rotations keep the sign of the
// key before them: between q and -q Max turns the long way round (the
// importer does the same, insertRotationKeys).
void writeRotKeys(Control* c, const Frames& f, const std::vector<bool>& bits, const std::vector<Quat>& keys)
{
    IKeyControl* k = GetKeyControlInterface(c);
    if (!k) return;
    k->SetNumKeys(0);
    Quat prev(0.0f, 0.0f, 0.0f, 1.0f);
    bool first = true;
    for (int i = 0; i < f.count; ++i) {
        if (!bits[i]) continue;
        Quat q = keys[i];
        if (!first && q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0) q = -q;
        prev = q;
        first = false;
        ILinRotKey key;
        memset(&key, 0, sizeof(key));
        key.time = f.at(i);
        key.val = q;
        k->AppendKey(&key);
    }
    k->SortKeys();
    changed(c);
}

void writePosKeys(Control* c, const Frames& f, const std::vector<bool>& bits, const std::vector<Point3>& vals)
{
    IKeyControl* k = GetKeyControlInterface(c);
    if (!k) return;
    k->SetNumKeys(0);
    for (int i = 0; i < f.count; ++i) {
        if (!bits[i]) continue;
        ILinPoint3Key key;
        memset(&key, 0, sizeof(key));
        key.time = f.at(i);
        key.val = vals[i];
        k->AppendKey(&key);
    }
    k->SortKeys();
    changed(c);
}

void writeScaleKeys(Control* c, const Frames& f, const std::vector<bool>& bits, const std::vector<Point3>& s,
                    const std::vector<Quat>& axis)
{
    IKeyControl* k = GetKeyControlInterface(c);
    if (!k) return;
    k->SetNumKeys(0);
    for (int i = 0; i < f.count; ++i) {
        if (!bits[i]) continue;
        ILinScaleKey key;
        memset(&key, 0, sizeof(key));
        key.time = f.at(i);
        key.val = ScaleValue(s[i], axis[i]);
        k->AppendKey(&key);
    }
    k->SortKeys();
    changed(c);
}

// In each gap between keys, the worst frame whose error is above 1 gets a key;
// returns how many were added. `dirty` gets the frames of every gap that got a
// key: only they can change, the next check looks at them alone.
int addKeysWhereMissing(std::vector<bool>& bits, const std::vector<double>& errs, std::vector<bool>* dirty = nullptr)
{
    int added = 0;
    double gw = 1.0;
    int ga = -1, prevKey = -1;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            if (ga >= 0) {
                bits[ga] = true;
                ++added;
                if (dirty)
                    for (size_t k = static_cast<size_t>(prevKey + 1); k < i; ++k) (*dirty)[k] = true;
            }
            prevKey = static_cast<int>(i);
            gw = 1.0;
            ga = -1;
        } else if (errs[i] > gw) {
            gw = errs[i];
            ga = static_cast<int>(i);
        }
    }
    return added;
}

// ─── Key selection ──────────────────────────────────────────────────────────

// Key frames so that linear position / slerp rotation of every channel stays
// within tolerance (greedy: the worst frame of each gap until none misses).
std::vector<bool> selectKeys(const std::vector<const std::vector<Point3>*>& posChans,
                             const std::vector<const std::vector<Quat>*>& rotChans,
                             int n, double posTol, double angTol)
{
    std::vector<bool> keyed(n, false);
    keyed[0] = true;
    keyed[n - 1] = true;
    std::vector<std::pair<int, int>> gaps{{0, n - 1}};
    while (!gaps.empty()) {
        std::vector<std::pair<int, int>> next;
        for (const auto& g : gaps) {
            const int a = g.first, b = g.second;
            double worst = 1.0;
            int wi = -1;
            for (int i = a + 1; i < b; ++i) {
                const float f = static_cast<float>(i - a) / static_cast<float>(b - a);
                double e = 0.0;
                for (const auto* c : posChans)
                    e = std::max(e, Length((*c)[a] + ((*c)[b] - (*c)[a]) * f - (*c)[i]) / posTol);
                for (const auto* c : rotChans) {
                    const Quat& qa = (*c)[a];
                    Quat qb = (*c)[b];
                    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) qb = -qb;
                    const Quat s = Slerp(qa, qb, f);
                    e = std::max(e, angleBetween(quatMatrix(s), quatMatrix((*c)[i])) / angTol);
                }
                if (e > worst) { worst = e; wi = i; }
            }
            if (wi > 0) {
                keyed[wi] = true;
                next.push_back({a, wi});
                next.push_back({wi, b});
            }
        }
        gaps.swap(next);
    }
    return keyed;
}

// ─── Scene ──────────────────────────────────────────────────────────────────

void collectNodes(INode* n, std::vector<INode*>& out)
{
    for (int i = 0; i < n->NumberOfChildren(); ++i) {
        INode* c = n->GetChildNode(i);
        out.push_back(c);
        collectNodes(c, out);
    }
}

std::vector<INode*> allNodes()
{
    std::vector<INode*> out;
    collectNodes(GetCOREInterface()->GetRootNode(), out);
    return out;
}

std::wstring userProp(INode* n, const wchar_t* key)
{
    MSTR v;
    if (n && n->GetUserPropString(key, v)) return std::wstring(v.data());
    return {};
}

bool isRigObject(INode* n) { return !userProp(n, L"WdxIKRig").empty(); }

bool isBone(INode* n)
{
    Object* o = n ? n->GetObjectRef() : nullptr;
    while (o && o->SuperClassID() == GEN_DERIVOB_CLASS_ID) o = static_cast<IDerivedObject*>(o)->GetObjRef();
    return o && o->ClassID() == BONE_OBJ_CLASSID;
}

bool isHelper(INode* n)
{
    Object* o = n ? n->GetObjectRef() : nullptr;
    while (o && o->SuperClassID() == GEN_DERIVOB_CLASS_ID) o = static_cast<IDerivedObject*>(o)->GetObjRef();
    return o && o->SuperClassID() == HELPER_CLASS_ID;
}

// Removes one line "key = ..." from the user properties.
void dropUserProp(INode* n, const wchar_t* key)
{
    MSTR buf;
    n->GetUserPropBuffer(buf);
    std::wstring in(buf.data()), out, line;
    std::wstringstream ss(in);
    const std::wstring k(key);
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t s = line.find_first_not_of(L" \t");
        std::wstring body = s == std::wstring::npos ? L"" : line.substr(s);
        if (body.size() >= k.size() && _wcsnicmp(body.c_str(), k.c_str(), k.size()) == 0) {
            std::wstring rest = body.substr(k.size());
            size_t e = rest.find_first_not_of(L" \t");
            if (e != std::wstring::npos && rest[e] == L'=') continue;
        }
        if (!line.empty()) out += line + L"\r\n";
    }
    n->SetUserPropBuffer(MSTR(out.c_str()));
}

// ─── Limb detection ─────────────────────────────────────────────────────────

int charKind(const std::wstring& s, int i)
{
    if (i < 0 || i >= static_cast<int>(s.size())) return 0;
    const wchar_t c = s[i];
    if (c >= L'A' && c <= L'Z') return 1;
    if (c >= L'a' && c <= L'z') return 2;
    if (c >= L'0' && c <= L'9') return 3;
    return 0;
}

// Lower-case words, split at anything not a letter/digit and at case changes:
// "LFoot_Heel" -> l foot heel.
std::vector<std::wstring> tokens(const std::wstring& s)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    for (int i = 0; i < static_cast<int>(s.size()); ++i) {
        const int k = charKind(s, i);
        if (k == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        const int prev = charKind(s, i - 1), next = charKind(s, i + 1);
        if (!cur.empty() && k == 1 && (prev == 2 || (prev == 1 && next == 2))) {
            out.push_back(cur);
            cur.clear();
        }
        cur += static_cast<wchar_t>(k == 1 ? s[i] - L'A' + L'a' : s[i]);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool hasAny(const std::vector<std::wstring>& toks, std::initializer_list<const wchar_t*> words)
{
    for (const auto& t : toks)
        for (const wchar_t* w : words)
            if (t == w) return true;
    return false;
}

const std::initializer_list<const wchar_t*> kWeaponWords = {
    L"weapon", L"sword", L"blade", L"katana", L"gun", L"pistol", L"rifle", L"axe", L"staff", L"spear",
    L"lance", L"bow", L"shield", L"scythe", L"stick", L"kunai", L"knife", L"dagger", L"hammer", L"mace",
    L"club", L"wand", L"rod", L"whip", L"fan", L"sickle"};

int sideOf(const std::vector<std::wstring>& toks)   // 1 left, 2 right, 0 none
{
    if (hasAny(toks, {L"l", L"left", L"lf", L"lft"})) return 1;
    if (hasAny(toks, {L"r", L"right", L"rt", L"rgt"})) return 2;
    return 0;
}

int depthOf(INode* n)
{
    int d = 0;
    for (INode* p = realParent(n); p; p = realParent(p)) ++d;
    return d;
}

double rootLen(INode* n, TimeValue t)
{
    double len = 0.0;
    for (INode* p = n; realParent(p); p = realParent(p))
        len += Length(p->GetNodeTM(t).GetTrans() - realParent(p)->GetNodeTM(t).GetTrans());
    return len;
}

double lineDist(const Point3& p, const Point3& a, const Point3& b)
{
    const float L = Length(b - a);
    if (L < 1e-6f) return Length(p - a);
    const Point3 u = (b - a) / L;
    const float s = DotProd(p - a, u);
    if (s < 0) return Length(p - a);
    if (s > L) return Length(p - b);
    return Length((p - a) - u * s);
}

bool collinear(INode* a, INode* b, const std::vector<INode*>& mids, const std::vector<TimeValue>& times)
{
    for (TimeValue t : times) {
        const Point3 pa = a->GetNodeTM(t).GetTrans(), pb = b->GetNodeTM(t).GetTrans();
        const double tol = std::max(0.05, 0.03 * Length(pb - pa));
        for (INode* m : mids)
            if (lineDist(m->GetNodeTM(t).GetTrans(), pa, pb) > tol) return false;
    }
    return true;
}

// The farthest ancestor whose in-between nodes stay on the line through the whole
// animation; zero-length steps are passed over (Blizzard's twist joints).
INode* jointAbove(INode* n, const std::vector<TimeValue>& times, double minLen)
{
    std::vector<INode*> anc;
    for (INode* p = realParent(n); p; p = realParent(p)) anc.push_back(p);
    INode* best = nullptr;
    std::vector<INode*> mids;
    for (size_t k = 0; k < anc.size(); ++k) {
        if (!collinear(anc[k], n, mids, times)) break;
        if (Length(anc[k]->GetNodeTM(times[0]).GetTrans() - n->GetNodeTM(times[0]).GetTrans()) >= minLen)
            best = anc[k];
        mids.push_back(anc[k]);
    }
    return best;
}

std::vector<TimeValue> sampleTimes(const Frames& f, int count)
{
    std::vector<TimeValue> out;
    for (int k = 0; k <= count; ++k) {
        const TimeValue t = f.at(static_cast<int>((static_cast<long long>(f.count - 1) * k) / count));
        if (out.empty() || out.back() != t) out.push_back(t);
    }
    return out;
}

double skeletonSize(const std::vector<INode*>& nodes, TimeValue t)
{
    Point3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    bool any = false;
    for (INode* n : nodes) {
        if (!isBone(n)) continue;
        const Point3 p = n->GetNodeTM(t).GetTrans();
        for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
        any = true;
    }
    if (!any) return 100.0;
    return std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
}

} // namespace

std::vector<Limb> detectLimbs(std::vector<std::wstring>* notes)
{
    const Frames f = sceneFrames();
    const TimeValue t0 = f.at(0);
    const std::vector<INode*> nodes = allNodes();
    struct End { int side; bool isLeg; INode* node; std::wstring how; };
    std::vector<End> ends;
    auto haveEnd = [&](int side, bool isLeg) {
        for (const auto& e : ends) if (e.side == side && e.isLeg == isLeg) return true;
        return false;
    };
    // 1. WC3 attachment points sit on the hand/foot
    for (INode* o : nodes) {
        if (!realParent(o) || isRigObject(o)) continue;
        const auto toks = tokens(o->GetName());
        if (std::find(toks.begin(), toks.end(), L"ref") == toks.end()) continue;
        const bool hand = hasAny(toks, {L"hand"}), foot = hasAny(toks, {L"foot"});
        const int side = sideOf(toks);
        if ((hand || foot) && side && !haveEnd(side, foot))
            ends.push_back({side, foot, realParent(o), std::wstring(L"attachment ") + o->GetName()});
    }
    // 2. names: fewest extra words, then highest in the tree
    for (int side = 1; side <= 2; ++side)
        for (int leg = 0; leg <= 1; ++leg) {
            if (haveEnd(side, leg != 0)) continue;
            INode* best = nullptr;
            int bestScore = 1 << 30;
            for (INode* o : nodes) {
                if (!realParent(o) || isRigObject(o) || !(isBone(o) || isHelper(o))) continue;
                const auto toks = tokens(o->GetName());
                const bool word = leg ? hasAny(toks, {L"foot", L"ankle"}) : hasAny(toks, {L"hand", L"wrist"});
                if (!word || sideOf(toks) != side) continue;
                if (hasAny(toks, {L"ref", L"finger", L"toe", L"thumb", L"nub", L"end", L"twist", L"weapon", L"attach", L"pad"}))
                    continue;
                const int score = static_cast<int>(toks.size()) * 100 + depthOf(o);
                if (score < bestScore) { best = o; bestScore = score; }
            }
            if (best) ends.push_back({side, leg != 0, best, L"name"});
        }
    // 3. legs without names: the lowest bone per side, from a toe up to where
    //    the chain turns vertical
    const double size = skeletonSize(nodes, t0);
    float zMin = 1e30f, zMax = -1e30f;
    for (INode* o : nodes)
        if (isBone(o) && !isRigObject(o)) {
            const float z = o->GetNodeTM(t0).GetTrans().z;
            zMin = std::min(zMin, z);
            zMax = std::max(zMax, z);
        }
    for (int side = 1; side <= 2; ++side) {
        if (haveEnd(side, true)) continue;
        const float sgn = side == 1 ? 1.0f : -1.0f;
        INode* low = nullptr;
        for (INode* o : nodes) {
            if (!isBone(o) || !realParent(o) || isRigObject(o)) continue;
            const Point3 p = o->GetNodeTM(t0).GetTrans();
            if (p.x * sgn > size * 0.02 && p.z < zMin + (zMax - zMin) * 0.35f)
                if (!low || p.z < low->GetNodeTM(t0).GetTrans().z) low = o;
        }
        if (!low) continue;
        INode* c = low;
        for (;;) {
            INode* p = realParent(c);
            if (!p) break;
            const Point3 d = c->GetNodeTM(t0).GetTrans() - p->GetNodeTM(t0).GetTrans();
            if (Length(d) > 1e-4f && std::fabs(d.z) < 0.5f * Length(d)) c = p;
            else break;
        }
        ends.push_back({side, true, c, L"lowest bone"});
    }
    // 4. elbow/knee and shoulder/hip from the motion
    const std::vector<TimeValue> times = sampleTimes(f, 60);
    std::vector<Limb> out;
    for (const auto& e : ends) {
        const std::wstring label = std::wstring(e.side == 1 ? L"L " : L"R ") + (e.isLeg ? L"Leg" : L"Arm");
        const double minLen = 0.08 * rootLen(e.node, t0);   // at least 8% of the way to the root
        INode* b = jointAbove(e.node, times, minLen);
        INode* a = b ? jointAbove(b, times, minLen) : nullptr;
        if (!a) {
            if (notes) notes->push_back(fmt(tr("ikrig_no_chain", L"%1: %2 has no two-joint chain above it"), {label, e.node->GetName()}));
            continue;
        }
        if (e.how == L"lowest bone") {
            const float za = a->GetNodeTM(t0).GetTrans().z, zb = b->GetNodeTM(t0).GetTrans().z,
                        zc = e.node->GetNodeTM(t0).GetTrans().z;
            if (!(za > zb && zb > zc)) {
                if (notes)
                    notes->push_back(fmt(tr("ikrig_not_down", L"%1: %2 > %3 > %4 does not run downward"),
                                         {label, a->GetName(), b->GetName(), e.node->GetName()}));
                continue;
            }
        }
        bool used = false;
        for (const auto& l : out) used = used || l.upper == a || l.lower == b || l.end == e.node;
        if (used) {
            if (notes) notes->push_back(fmt(tr("ikrig_used", L"%1: %2 is already part of another limb"), {label, e.node->GetName()}));
            continue;
        }
        out.push_back({label, a, b, e.node, e.isLeg, e.how});
    }
    return out;
}

static bool g_debugTiming = false;
void setDebugTiming(bool on) { g_debugTiming = on; }

bool hasRig()
{
    for (INode* n : allNodes())
        if (userProp(n, L"WdxIKRig") == L"master") return true;
    return false;
}

namespace {

// ─── Progress ───────────────────────────────────────────────────────────────

DWORD WINAPI noWork(LPVOID) { return 0; }

struct Progress {
    bool on = false;
    Progress(bool show, const std::wstring& title)
    {
        on = show;
        if (on) GetCOREInterface()->ProgressStart(const_cast<wchar_t*>(title.c_str()), TRUE, noWork, nullptr);
    }
    ~Progress() { if (on) GetCOREInterface()->ProgressEnd(); }
    // false when the user cancelled
    bool update(double pct)
    {
        if (!on) return true;
        GetCOREInterface()->ProgressUpdate(static_cast<int>(pct));
        return !GetCOREInterface()->GetCancel();
    }
};

// Undo off, redraw off, animate off while the rig is built.
struct SceneQuiet {
    SceneQuiet()
    {
        theHold.Suspend();
        GetCOREInterface()->DisableSceneRedraw();
        SuspendAnimate();
        AnimateOff();
    }
    ~SceneQuiet()
    {
        ResumeAnimate();
        GetCOREInterface()->EnableSceneRedraw();
        theHold.Resume();
    }
};

// ─── One limb ───────────────────────────────────────────────────────────────

Point3 poleAt(const Point3& s, const Point3& e, const Point3& w, const Matrix3& aRot, const Point3& nRef, float size)
{
    const Point3 axis = Normalize(w - s);
    Point3 perp = (e - s) - axis * DotProd(e - s, axis);
    const Point3 side = Normalize(CrossProd(axis, nRef * aRot));
    if (Length(perp) < size * 1e-3f) return e + side * (size * 0.5f);
    perp = Normalize(perp);
    if (DotProd(perp, side) < 0) perp = -perp;
    return e + perp * (size * 0.5f);
}

struct LimbResult {
    bool ok = false, cancelled = false;
    int keys = 0;
    double worstPos = 0, twistMax = 0, twistSmallPct = 0, size = 0;
    std::wstring error;
    std::wstring timing;   // per stage, only when asked for (wdxIKRigBuild debug:true)
};

LimbResult buildLimb(const Limb& limb, ULONG masterHandle, const Frames& f, Progress& prog, double base, double span)
{
    LimbResult res;
    DWORD stageStart = GetTickCount();
    auto stage = [&](const wchar_t* name) {
        const DWORD now = GetTickCount();
        res.timing += std::wstring(name) + L" " + std::to_wstring(now - stageStart) + L"ms ";
        stageStart = now;
    };
    Interface* ip = GetCOREInterface();
    INode* nodes[3] = {limb.upper, limb.lower, limb.end};
    const int nf = f.count;

    // 1. the original world transforms and local rotations on every frame
    std::vector<Matrix3> wT[3], want[3];
    for (int j = 0; j < 3; ++j) { wT[j].resize(nf); want[j].resize(nf); }
    for (int i = 0; i < nf; ++i)
        for (int j = 0; j < 3; ++j) {
            wT[j][i] = nodes[j]->GetNodeTM(f.at(i));
            want[j][i] = localRot(nodes[j], f.at(i));
        }
    if (!prog.update(base + span * 0.1)) { res.cancelled = true; return res; }

    stage(L"read");
    // 2. the setup frame: the most bent among the frames bending the usual way
    std::vector<double> bends(nf, 0.0);
    std::vector<Point3> normals(nf, Point3(0, 0, 0));
    Point3 usual(0, 0, 0);
    for (int i = 0; i < nf; ++i) {
        const Point3 u = wT[1][i].GetTrans() - wT[0][i].GetTrans(), v = wT[2][i].GetTrans() - wT[1][i].GetTrans();
        if (Length(u) < 1e-6f || Length(v) < 1e-6f) continue;
        bends[i] = std::acos(std::max(-1.0f, std::min(1.0f, DotProd(Normalize(u), Normalize(v)))));
        const Point3 c = CrossProd(u, v);
        if (Length(c) > 1e-9f) normals[i] = Normalize(c) * Inverse(unitRot(wT[0][i]));
        usual += normals[i] * static_cast<float>(bends[i]);
    }
    const bool haveUsual = Length(usual) > 1e-9f;
    if (haveUsual) usual = Normalize(usual);
    int rb = 0;
    double bendMax = -1.0;
    for (int i = 0; i < nf; ++i)
        if (bends[i] > bendMax && (!haveUsual || DotProd(normals[i], usual) > 0.8f)) { bendMax = bends[i]; rb = i; }
    const TimeValue tb = f.at(rb);
    const Point3 S = wT[0][rb].GetTrans(), E = wT[1][rb].GetTrans(), W = wT[2][rb].GetTrans();
    const float size = Length(E - S) + Length(W - E);
    res.size = size;
    Point3 nRef = Normalize(CrossProd(E - S, W - E)) * Inverse(unitRot(wT[0][rb]));
    {
        const Point3 ax0 = Normalize(W - S);
        const Point3 perp0 = Normalize((E - S) - ax0 * DotProd(E - S, ax0));
        if (DotProd(perp0, Normalize(CrossProd(ax0, nRef * unitRot(wT[0][rb])))) < 0) nRef = -nRef;
    }
    const Point3 pole0 = poleAt(S, E, W, unitRot(wT[0][rb]), nRef, size);

    // 3. the objects (MaxScript: a handful per limb)
    Matrix3 ctrl0 = unitRot(wT[2][rb]);
    ctrl0.SetTrans(W);
    std::wostringstream sc;
    sc << L"(local A = maxOps.getNodeByHandle " << limb.upper->GetHandle()
       << L"; local B = maxOps.getNodeByHandle " << limb.lower->GetHandle()
       << L"; local C = maxOps.getNodeByHandle " << limb.end->GetHandle()
       << L"; local M = maxOps.getNodeByHandle " << masterHandle
       << L"; local lbl = " << mxsString(limb.label.c_str())
       << L"; local ctlL = LayerManager.getLayerFromName \"IK Rig\""
       << L"; local drvL = LayerManager.getLayerFromName \"IK Rig Drivers\""
       << L"; local old = sliderTime; sliderTime = " << (tb / f.tpf)
       << L"; local S = " << mxs(S) << L", E = " << mxs(E) << L", W = " << mxs(W) << L", size = " << mxs(size)
       << L"; local d1 = BoneSys.createBone S E " << mxs(unitRot(wT[0][rb]).GetRow(2))
       << L"; local d2 = BoneSys.createBone E W " << mxs(unitRot(wT[1][rb]).GetRow(2))
       << L"; local d3 = BoneSys.createBone W (W + (normalize (W - E)) * size * 0.1) " << mxs(unitRot(wT[1][rb]).GetRow(2))
       << L"; d1.name = \"IK \" + lbl + \" Driver 1\"; d2.name = \"IK \" + lbl + \" Driver 2\"; d3.name = \"IK \" + lbl + \" Driver 3\""
       << L"; for d in #(d1, d2, d3) do (d.width = size * 0.02; d.height = size * 0.02; setUserProp d \"WdxIKRig\" \"driver\"; setUserProp d \"WdxNoExport\" 1; drvL.addNode d)"
       << L"; d2.parent = d1; d3.parent = d2; d1.parent = A.parent; d1.pos.controller = copy A.pos.controller"
       << L"; local ctrl = " << (limb.isLeg ? L"Rectangle length:(size * 0.25) width:(size * 0.15)" : L"Circle radius:(size * 0.08)")
       << L"; ctrl.name = \"IK \" + lbl; ctrl.wirecolor = " << (limb.isLeg ? L"(color 30 160 230)" : L"(color 140 110 230)")
       << L"; ctrl.parent = M; ctrl.pos.controller = Linear_Position(); ctrl.rotation.controller = Linear_Rotation()"
       << L"; ctrl.transform = " << mxs(ctrl0)
       << L"; setUserProp ctrl \"WdxIKRig\" \"control\"; setUserProp ctrl \"WdxNoExport\" 1; ctlL.addNode ctrl"
       << L"; local pole = Ellipse length:(size * 0.08) width:(size * 0.08); pole.name = \"IK \" + lbl + \" Pole\"; pole.wirecolor = color 40 200 180"
       << L"; pole.parent = ctrl; pole.pos.controller = Linear_Position(); pole.pos = " << mxs(pole0)
       << L"; setUserProp pole \"WdxIKRig\" \"pole\"; setUserProp pole \"WdxNoExport\" 1; ctlL.addNode pole"
       << L"; local ik = IKSys.ikChain d1 d3 \"IKLimb\"; ik.name = \"IK \" + lbl + \" Chain\""
       << L"; ik.transform.controller.VHTarget = pole; ik.transform.controller.useVHTarget = true; ik.parent = ctrl"
       << L"; setUserProp ik \"WdxIKRig\" \"chain\"; setUserProp ik \"WdxNoExport\" 1; drvL.addNode ik"
       << L"; local roles = #(\"upper\", \"lower\", \"end\"), tg = #(d1, d2, ctrl), bn = #(A, B, C)"
       << L"; for j = 1 to 3 do (local rl = rotation_list(); bn[j].rotation.controller = rl"
       << L"; rl.available.controller = Orientation_Constraint(); rl[2].controller.appendTarget tg[j] 100"
       << L"; rl.available.controller = Linear_Rotation(); rl.available.controller = Linear_Rotation(); rl.delete 1"
       << L"; rl.setName 1 \"IK\"; rl.setName 2 \"Offset\"; rl.setName 3 \"Twist\"; rl.setActive 3"
       << L"; setUserProp bn[j] \"WdxIKRigBone\" (lbl + \"|\" + roles[j]))"
       << L"; sliderTime = old"
       << L"; (d1.inode.handle as string) + \",\" + (d2.inode.handle as string) + \",\" + (d3.inode.handle as string) + \",\" + (ctrl.inode.handle as string) + \",\" + (pole.inode.handle as string) + \",\" + (ik.inode.handle as string))";
    const std::vector<ULONG> h = parseHandles(runScript(sc.str()));
    if (h.size() != 6) { res.error = L"the rig objects could not be created"; return res; }
    INode* d[3] = {ip->GetINodeByHandle(h[0]), ip->GetINodeByHandle(h[1]), ip->GetINodeByHandle(h[2])};
    INode* ctrl = ip->GetINodeByHandle(h[3]);
    INode* pole = ip->GetINodeByHandle(h[4]);
    INode* ik = ip->GetINodeByHandle(h[5]);
    if (!d[0] || !ctrl || !pole || !ik) { res.error = L"the rig objects could not be found"; return res; }
    Control* cPos = ctrl->GetTMController()->GetPositionController();
    Control* cRot = ctrl->GetTMController()->GetRotationController();
    Control* pPos = pole->GetTMController()->GetPositionController();
    Control* offset[3] = {};
    Control* twist[3] = {};
    for (int j = 0; j < 3; ++j) {
        IListControl* list = GetIListControlInterface(nodes[j]->GetTMController()->GetRotationController());
        if (!list || list->GetListCount() < 3) { res.error = L"the rotation list is missing"; return res; }
        offset[j] = list->GetSubCtrl(1);
        twist[j] = list->GetSubCtrl(2);
    }

    stage(L"setup");
    // 4. the constant offset, taken in the setup frame: the controls stand there
    //    without any key yet
    Matrix3 offs[3];
    for (int j = 0; j < 3; ++j) {
        offs[j] = want[j][rb] * Inverse(localRot(nodes[j], tb));
        setConstRot(offset[j], offs[j]);   // one key: the same on every frame
    }

    // 5. exact bake on every frame, in time order; the control values are kept
    std::vector<Point3> cP(nf), pP(nf);
    std::vector<Quat> cR(nf);
    std::vector<Quat> tw[3];
    for (int j = 0; j < 3; ++j) tw[j].resize(nf);
    auto bakeTwist = [&](int i) {
        const TimeValue t = f.at(i);
        for (int j = 0; j < 3; ++j) {
            const Matrix3 twm = rotationAt(twist[j], t);
            const Matrix3 cons = Inverse(offs[j]) * Inverse(twm) * localRot(nodes[j], t);
            const Matrix3 r = want[j][i] * Inverse(cons) * Inverse(offs[j]);
            setConstRot(twist[j], r);
            tw[j][i] = keyFor(r);
        }
    };
    for (int i = 0; i < nf; ++i) {
        const TimeValue t = f.at(i);
        const Matrix3 hand = unitRot(wT[2][i]);
        cP[i] = wT[2][i].GetTrans();
        cR[i] = keyFor(hand);
        setConstPos(cPos, cP[i]);
        setConstRot(cRot, hand);
        const Point3 pw = poleAt(wT[0][i].GetTrans(), wT[1][i].GetTrans(), wT[2][i].GetTrans(), unitRot(wT[0][i]), nRef, size);
        pP[i] = (pw * Inverse(ctrl->GetNodeTM(t)));
        setConstPos(pPos, pP[i]);
        bakeTwist(i);
        if ((i & 63) == 0 && !prog.update(base + span * (0.1 + 0.5 * i / nf))) { res.cancelled = true; return res; }
    }

    stage(L"bake");
    // 6. the controls get keys where their own path needs them
    const double posTol = std::max(0.002, size * 1e-4), angTol = 0.02;
    const std::vector<bool> bitsC = selectKeys({&cP}, {&cR}, nf, posTol, angTol);
    const std::vector<bool> bitsP = selectKeys({&pP}, {}, nf, posTol, angTol);
    writePosKeys(cPos, f, bitsC, cP);
    writeRotKeys(cRot, f, bitsC, cR);
    writePosKeys(pPos, f, bitsP, pP);

    stage(L"controls");
    // 7. the twist once more, against what the interpolated controls give
    for (int j = 0; j < 3; ++j) clearKeys(twist[j]);
    for (int i = 0; i < nf; ++i) {
        bakeTwist(i);
        if ((i & 63) == 0 && !prog.update(base + span * (0.6 + 0.2 * i / nf))) { res.cancelled = true; return res; }
    }

    stage(L"twist");
    // 8. twist keys per bone; every frame checked, a key added in each gap where a frame misses
    std::vector<bool> bitsT[3];
    for (int j = 0; j < 3; ++j) bitsT[j] = selectKeys({}, {&tw[j]}, nf, posTol, angTol);
    bool done = false;
    double worstPos = 0;
    std::vector<double> errs[3], dist[3];
    for (int j = 0; j < 3; ++j) { errs[j].assign(nf, 0.0); dist[j].assign(nf, 0.0); }
    // the first round looks at every frame, later ones only at the gaps that got a key, so
    // up to 40 rounds stay cheap (10 were too few for one leg of the Paladin in Max 2016)
    std::vector<bool> dirty(nf, true);
    for (int round = 0; round < 40 && !done; ++round) {
        for (int j = 0; j < 3; ++j) writeRotKeys(twist[j], f, bitsT[j], tw[j]);
        for (int i = 0; i < nf; ++i) {
            if (!dirty[i]) continue;
            dirty[i] = false;
            for (int j = 0; j < 3; ++j) {
                const Matrix3 m = nodes[j]->GetNodeTM(f.at(i));
                dist[j][i] = Length(m.GetTrans() - wT[j][i].GetTrans());
                errs[j][i] = std::max(dist[j][i] / (posTol * 4), rowDiff(m, wT[j][i]) / 0.001);
            }
        }
        int added = 0;
        for (int j = 0; j < 3; ++j) added += addKeysWhereMissing(bitsT[j], errs[j], &dirty);
        done = added == 0;
    }
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < nf; ++i) worstPos = std::max(worstPos, dist[j][i]);
    for (INode* n : {d[0], d[1], d[2], ik}) if (n) n->Hide(TRUE);

    int keys = 0;
    for (int i = 0; i < nf; ++i) keys += (bitsC[i] ? 2 : 0) + (bitsP[i] ? 1 : 0) + bitsT[0][i] + bitsT[1][i] + bitsT[2][i];
    double twMax = 0;
    int smallCount = 0;
    for (int i = 0; i < nf; ++i) {
        double a = 0;
        for (int j = 0; j < 2; ++j)
            a = std::max(a, angleBetween(quatMatrix(rotationConjugate() ? conjugated(tw[j][i]) : tw[j][i]), identity3()));
        twMax = std::max(twMax, a);
        if (a < 2.0) ++smallCount;
    }
    stage(L"verify");
    res.ok = done;
    res.keys = keys;
    res.worstPos = worstPos;
    res.twistMax = twMax;
    res.twistSmallPct = 100.0 * smallCount / nf;
    return res;
}

// ─── Weapons ────────────────────────────────────────────────────────────────

Matrix3 relOf(const Matrix3& w, const Matrix3& h) { return w * Inverse(h); }

// The PRS keys of a node (its own or a link constraint's): set to the world
// transforms wT on every frame, then reduced and checked.
std::wstring g_weaponTiming;   // filled while debug timing is on

bool keyWorldTMs(INode* w, const std::vector<Matrix3>& wT, const Frames& f, double& worst)
{
    const DWORD kwStart = GetTickCount();
    const int nf = f.count;
    Control* tm = w->GetTMController();
    // A Link Constraint keeps its own PRS ("link_params", its first sub-anim)
    if (tm && !tm->GetPositionController() && tm->NumSubs() > 0)
        if (Animatable* a = tm->SubAnim(0))
            if (a->SuperClassID() == CTRL_MATRIX3_CLASS_ID) tm = static_cast<Control*>(a);
    Control* pc = tm ? tm->GetPositionController() : nullptr;
    Control* rc = tm ? tm->GetRotationController() : nullptr;
    Control* sc = tm ? tm->GetScaleController() : nullptr;
    if (!pc || !rc || !sc) return false;
    std::vector<Point3> P(nf), SV(nf);
    std::vector<Quat> R(nf), SQ(nf);
    // With the PRS at rest (no move, no turn, scale 1) the node shows what places
    // it - its parent, or the link target of that frame. The PRS values are then
    // the world transform seen from there. (Setting the world transform frame by
    // frame instead, SetNodeTM through the Link Constraint, took Hidan's scythe
    // two minutes.)
    setConstPos(pc, Point3(0.0f, 0.0f, 0.0f));
    setConstRot(rc, identity3());
    if (IKeyControl* k = GetKeyControlInterface(sc)) {
        k->SetNumKeys(0);
        ILinScaleKey key;
        memset(&key, 0, sizeof(key));
        key.val = ScaleValue(Point3(1.0f, 1.0f, 1.0f));
        k->AppendKey(&key);
        changed(sc);
    }
    for (int i = 0; i < nf; ++i) {
        const Matrix3 local = wT[i] * Inverse(w->GetNodeTM(f.at(i)));
        P[i] = local.GetTrans();
        R[i] = keyFor(polar(local));
        AffineParts ap;
        decomp_affine(local, &ap);
        SV[i] = ap.k * ap.f;
        SQ[i] = ap.u;
    }
    double size = 0;
    for (const auto& p : P) size = std::max(size, static_cast<double>(Length(p)));
    const double posTol = std::max(0.002, size * 1e-5);
    const DWORD kwRead = GetTickCount();
    std::vector<bool> bits = selectKeys({&P, &SV}, {&R}, nf, posTol, 0.02);
    const DWORD kwSel = GetTickCount();
    int rounds = 0;
    bool done = false;
    std::vector<double> errs(nf, 0.0), dist(nf, 0.0);
    std::vector<bool> dirty(nf, true);   // later rounds look only where keys were added
    for (int round = 0; round < 40 && !done; ++round) {
        writePosKeys(pc, f, bits, P);
        writeRotKeys(rc, f, bits, R);
        writeScaleKeys(sc, f, bits, SV, SQ);
        for (int i = 0; i < nf; ++i) {
            if (!dirty[i]) continue;
            dirty[i] = false;
            const Matrix3 m = w->GetNodeTM(f.at(i));
            dist[i] = Length(m.GetTrans() - wT[i].GetTrans());
            errs[i] = std::max(dist[i] / (posTol * 4), rowDiff(m, wT[i]) / 0.001);
        }
        done = addKeysWhereMissing(bits, errs, &dirty) == 0;
        ++rounds;
    }
    worst = 0;
    for (double d : dist) worst = std::max(worst, d);
    {
        int nk = 0;
        for (bool b : bits) nk += b;
        g_weaponTiming += std::wstring(w->GetName()) + L": read " + std::to_wstring(kwRead - kwStart) + L"ms select " + std::to_wstring(kwSel - kwRead) +
                          L"ms rounds " + std::to_wstring(rounds) + L" " + std::to_wstring(GetTickCount() - kwSel) + L"ms keys " + std::to_wstring(nk) + L"; ";
    }
    return done;
}

int linkWeapons(const std::vector<Limb>& limbs, const Frames& f, std::vector<std::wstring>& report)
{
    const int nf = f.count;
    std::vector<INode*> hands, skip, uppers;
    for (const auto& l : limbs) {
        if (!l.isLeg) hands.push_back(l.end);
        skip.insert(skip.end(), {l.upper, l.lower, l.end});
        uppers.push_back(l.upper);
    }
    if (hands.empty()) return 0;
    const double tolR = 0.004;   // about 0.25 degrees
    const std::vector<TimeValue> samples = sampleTimes(f, 60);
    struct Found { INode* w; INode* hand; std::vector<std::pair<int, int>> runs; int cover; };
    std::vector<Found> found;
    // moves at all (on the sample frames)?
    auto movesAt = [&](INode* n) {
        const Matrix3 s0 = n->GetNodeTM(samples[0]);
        for (TimeValue t : samples) {
            const Matrix3 m = n->GetNodeTM(t);
            if (Length(m.GetTrans() - s0.GetTrans()) > 0.001 || rowDiff(m, s0) > 0.0001) return true;
        }
        return false;
    };
    for (INode* w : allNodes()) {
        if (isRigObject(w) || std::find(skip.begin(), skip.end(), w) != skip.end()) continue;
        if (!userProp(w, L"WdxIKRigBone").empty()) continue;
        if (!hasAny(tokens(w->GetName()), kWeaponWords)) continue;
        bool under = false;
        for (INode* h : hands) under = under || w == h || isAncestor(w, h);
        for (INode* u : uppers) under = under || isAncestor(u, w);   // follows the limb already
        if (under) continue;
        Control* tm = w->GetTMController();
        if (!tm || tm->ClassID() != Class_ID(PRS_CONTROL_CLASS_ID, 0)) continue;
        if (!movesAt(w)) continue;
        // below a moving weapon it goes with that one (Hidan's scythe: dozens of
        // rope and blade bones, each checked frame by frame took 17 s)
        bool belowWeapon = false;
        for (INode* p = realParent(w); p && !belowWeapon; p = realParent(p))
            belowWeapon = hasAny(tokens(p->GetName()), kWeaponWords) && movesAt(p);
        if (belowWeapon) continue;
        Found best{w, nullptr, {}, 0};
        for (INode* h : hands) {
            double limbSize = 0;
            INode* base = nullptr;
            for (const auto& l : limbs)
                if (l.end == h) {
                    limbSize = Length(l.upper->GetNodeTM(f.at(0)).GetTrans() - l.lower->GetNodeTM(f.at(0)).GetTrans()) +
                               Length(l.lower->GetNodeTM(f.at(0)).GetTrans() - h->GetNodeTM(f.at(0)).GetTrans());
                    base = realParent(l.upper);
                }
            // held: the weapon's pivot keeps its place in the hand's space (its own turning is left to its keys)
            const double tolP = std::max(0.01, limbSize * 0.02);
            int hits = 0;
            for (size_t k = 1; k < samples.size(); ++k) {
                const Matrix3 r1 = relOf(w->GetNodeTM(samples[k - 1]), h->GetNodeTM(samples[k - 1]));
                const Matrix3 r2 = relOf(w->GetNodeTM(samples[k]), h->GetNodeTM(samples[k]));
                if (Length(r1.GetTrans() - r2.GetTrans()) <= tolP) ++hits;
            }
            if (hits < 3) continue;
            std::vector<Matrix3> wT(nf), hT(nf), bT(nf);
            for (int i = 0; i < nf; ++i) {
                wT[i] = w->GetNodeTM(f.at(i));
                hT[i] = h->GetNodeTM(f.at(i));
                bT[i] = base ? base->GetNodeTM(f.at(i)) : identity3();
            }
            // runs; a run counts only while the hand moves against the body (with the arm
            // still, everything on the body keeps its place beside the hand)
            std::vector<std::pair<int, int>> runs;
            int a = 0;
            Matrix3 ref = relOf(wT[0], hT[0]);
            for (int i = 1; i <= nf; ++i) {
                const bool held = i < nf && Length(relOf(wT[i], hT[i]).GetTrans() - ref.GetTrans()) <= tolP;
                if (held) continue;
                if (i - a >= 5) {
                    bool moved = false;
                    const Matrix3 hb0 = relOf(hT[a], bT[a]);
                    for (int k = a + 1; k < i && !moved; ++k) {
                        const Matrix3 hb = relOf(hT[k], bT[k]);
                        moved = Length(hb.GetTrans() - hb0.GetTrans()) > tolP * 5 || rowDiff(hb, hb0) > tolR * 20;
                    }
                    if (moved) runs.push_back({a, i - 1});
                }
                if (i < nf) { a = i; ref = relOf(wT[i], hT[i]); }
            }
            // Holding phases a few frames apart become one: the weapon's own keys
            // bridge the gap exactly, and every switch of a Link Constraint costs on
            // each evaluation (Hidan's scythe: 193 phases, 3.3 ms per frame).
            std::vector<std::pair<int, int>> merged;
            for (const auto& r : runs) {
                if (!merged.empty() && r.first - merged.back().second - 1 <= 10) merged.back().second = r.second;
                else merged.push_back(r);
            }
            runs.swap(merged);
            int cover = 0;
            for (const auto& r : runs) cover += r.second - r.first + 1;
            if (cover > best.cover) best = {w, h, runs, cover};
        }
        if (best.hand) found.push_back(best);
    }
    // only the topmost: a weapon's own children go with it already
    int linked = 0;
    for (const auto& fw : found) {
        bool below = false;
        for (const auto& g : found) below = below || (g.w != fw.w && isAncestor(g.w, fw.w));
        if (below) continue;
        std::vector<Matrix3> wT(nf);
        for (int i = 0; i < nf; ++i) wT[i] = fw.w->GetNodeTM(f.at(i));
        std::wostringstream sc;
        sc << L"(local w = maxOps.getNodeByHandle " << fw.w->GetHandle() << L"; local h = maxOps.getNodeByHandle " << fw.hand->GetHandle()
           << L"; local lc = Link_Constraint(); w.transform.controller = lc; lc.addWorld frameNo:" << f.first;
        for (const auto& r : fw.runs) {
            sc << L"; lc.addTarget h " << (f.first + r.first);
            if (r.second < nf - 1) sc << L"; lc.addWorld frameNo:" << (f.first + r.second + 1);
        }
        sc << L"; w.pos.controller = Linear_Position(); w.rotation.controller = Linear_Rotation(); w.scale.controller = Linear_Scale()"
           << L"; setUserProp w \"WdxIKRigWeapon\" h.name; \"ok\")";
        if (runScript(sc.str()) != L"ok") {
            report.push_back(std::wstring(fw.w->GetName()) + L": the Link Constraint could not be set");
            continue;
        }
        double worst = 0;
        const bool ok = keyWorldTMs(fw.w, wT, f, worst);
        if (ok) {
            ++linked;
            report.push_back(fmt(tr("ikrig_weapon", L"Weapon %1: linked to %2 in %3 phase(s), %4 of %5 frames, largest deviation %6"),
                                 {fw.w->GetName(), fw.hand->GetName(), std::to_wstring(fw.runs.size()), std::to_wstring(fw.cover),
                                  std::to_wstring(nf), num(worst, 4)}));
        } else
            report.push_back(fmt(tr("ikrig_weapon_off", L"Weapon %1: frames still deviate by %2 - check it"), {fw.w->GetName(), num(worst, 4)}));
    }
    return linked;
}

} // namespace

Result build(const std::vector<Limb>* which, bool showProgress)
{
    Result res;
    if (hasRig()) {
        res.lines.push_back(tr("ikrig_exists", L"The scene already has an IK rig. Remove it first."));
        return res;
    }
    std::vector<Limb> detected;
    if (!which) {
        detected = detectLimbs(&res.lines);
        which = &detected;
    }
    if (which->empty()) {
        res.lines.push_back(tr("ikrig_none", L"No arm or leg found."));
        return res;
    }
    const DWORD start = GetTickCount();
    const Frames f = sceneFrames();
    Progress prog(showProgress, tr("ikrig_progress", L"Building IK rig..."));
    {
        SceneQuiet quiet;
        // master and layers
        std::vector<INode*> nodes = allNodes();
        const double skel = skeletonSize(nodes, f.at(0));
        std::wostringstream sc;
        sc << L"(for n in #(\"IK Rig\", \"IK Rig Drivers\") do if LayerManager.getLayerFromName n == undefined do LayerManager.newLayerFromName n"
           << L"; local m = Star radius1:" << mxs(static_cast<float>(skel * 0.3)) << L" radius2:" << mxs(static_cast<float>(skel * 0.22)) << L" points:8"
           << L"; m.name = \"IK Master\"; m.wirecolor = color 230 200 60; setUserProp m \"WdxIKRig\" \"master\"; setUserProp m \"WdxNoExport\" 1"
           << L"; (LayerManager.getLayerFromName \"IK Rig\").addNode m; m.inode.handle as string)";
        const std::vector<ULONG> mh = parseHandles(runScript(sc.str()));
        if (mh.size() != 1) {
            res.lines.push_back(L"Error: the IK master could not be created");
            return res;
        }
        for (size_t k = 0; k < which->size() && !res.cancelled; ++k) {
            const Limb& l = (*which)[k];
            const LimbResult r = buildLimb(l, mh[0], f, prog, 100.0 * k / which->size(), 100.0 / which->size());
            if (r.cancelled) { res.cancelled = true; break; }
            if (!r.error.empty())
                res.lines.push_back(l.label + L": " + r.error);
            else if (r.ok) {
                ++res.limbs;
                res.lines.push_back(fmt(tr("ikrig_limb_done", L"%1 (%2 > %3 > %4): %5 keys, largest deviation %6 (limb length %7), twist up to %8 deg, under 2 deg on %9% of frames"),
                                        {l.label, l.upper->GetName(), l.lower->GetName(), l.end->GetName(), std::to_wstring(r.keys),
                                         num(r.worstPos, 4), num(r.size, 1), num(r.twistMax, 0), num(r.twistSmallPct, 0)}));
                if (g_debugTiming) res.lines.push_back(L"  " + r.timing);
            } else
                res.lines.push_back(fmt(tr("ikrig_limb_off", L"%1: frames still deviate by %2 - check this limb"), {l.label, num(r.worstPos, 4)}));
        }
        const DWORD weaponStart = GetTickCount();
        g_weaponTiming.clear();
        if (!res.cancelled) res.weapons = linkWeapons(*which, f, res.lines);
        if (g_debugTiming) res.lines.push_back(L"  weapons " + std::to_wstring(GetTickCount() - weaponStart) + L"ms  " + g_weaponTiming);
        // hidden only now: hidden from the start, the drivers did not follow their IK goal while baking
        runScript(L"(local l = LayerManager.getLayerFromName \"IK Rig Drivers\"; if l != undefined do l.isHidden = true; \"\")");
    }
    if (res.cancelled) res.lines.push_back(tr("ikrig_cancelled", L"Cancelled - the limbs built so far stay; Remove takes them off again."));
    res.lines.push_back(fmt(tr("ikrig_time", L"%1 limbs in %2 s"), {std::to_wstring(res.limbs), num((GetTickCount() - start) / 1000.0, 1)}));
    GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
    return res;
}

Result remove(bool showProgress)
{
    Result res;
    std::vector<INode*> bones, weapons, rig;
    for (INode* n : allNodes()) {
        if (!userProp(n, L"WdxIKRigBone").empty()) bones.push_back(n);
        if (!userProp(n, L"WdxIKRigWeapon").empty()) weapons.push_back(n);
        if (isRigObject(n)) rig.push_back(n);
    }
    if (bones.empty() && rig.empty() && weapons.empty()) {
        res.lines.push_back(tr("ikrig_no_rig", L"The scene has no IK rig."));
        return res;
    }
    const Frames f = sceneFrames();
    const int nf = f.count;
    Progress prog(showProgress, tr("ikrig_progress_remove", L"Removing IK rig..."));
    {
        SceneQuiet quiet;
        // every bone's local rotation and every weapon's world transform, before anything changes
        std::vector<std::vector<Quat>> vals(bones.size(), std::vector<Quat>(nf));
        std::vector<std::vector<Matrix3>> wvals(weapons.size(), std::vector<Matrix3>(nf));
        for (int i = 0; i < nf; ++i) {
            for (size_t k = 0; k < bones.size(); ++k) vals[k][i] = keyFor(localRot(bones[k], f.at(i)));
            for (size_t k = 0; k < weapons.size(); ++k) wvals[k][i] = weapons[k]->GetNodeTM(f.at(i));
            if ((i & 63) == 0) prog.update(50.0 * i / nf);
        }
        // weapons: a plain PRS again (one that fails must not keep the rest of the rig in the scene)
        for (size_t k = 0; k < weapons.size(); ++k) {
            INode* w = weapons[k];
            const std::wstring ok = runScript(
                L"(local w = maxOps.getNodeByHandle " + std::to_wstring(w->GetHandle()) +
                L"; w.transform.controller = prs(); w.pos.controller = Linear_Position(); w.rotation.controller = Linear_Rotation(); w.scale.controller = Linear_Scale(); \"ok\")");
            double worst = 0;
            if (ok != L"ok" || !keyWorldTMs(w, wvals[k], f, worst))
                res.lines.push_back(std::wstring(w->GetName()) + L": could not be keyed again");
            dropUserProp(w, L"WdxIKRigWeapon");
        }
        for (size_t k = 0; k < bones.size(); ++k) {
            INode* b = bones[k];
            Control* c = static_cast<Control*>(CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
            b->GetTMController()->SetRotationController(c);
            std::vector<bool> bits = selectKeys({}, {&vals[k]}, nf, 1.0, 0.02);
            // every frame checked: the local rotation back where it was
            std::vector<double> errs(nf, 0.0);
            std::vector<bool> dirty(nf, true);
            for (int round = 0; round < 40; ++round) {
                writeRotKeys(c, f, bits, vals[k]);
                for (int i = 0; i < nf; ++i) {
                    if (!dirty[i]) continue;
                    dirty[i] = false;
                    errs[i] = angleBetween(rotationAt(c, f.at(i)),
                                           quatMatrix(rotationConjugate() ? conjugated(vals[k][i]) : vals[k][i])) / 0.05;
                }
                if (addKeysWhereMissing(bits, errs, &dirty) == 0) break;
            }
            dropUserProp(b, L"WdxIKRigBone");
        }
        // Deleted in one MaxScript call: deleting a driver bone takes its IK chain
        // with it, so pointers collected before would dangle (and INode-wise
        // deletion left the chains in the scene).
        runScript(L"(delete (for o in objects where getUserProp o \"WdxIKRig\" != undefined collect o)"
                  L"; for n in #(\"IK Rig\", \"IK Rig Drivers\") do (local l = LayerManager.getLayerFromName n; if l != undefined do (local ns = #(); l.nodes &ns; if ns.count == 0 do LayerManager.deleteLayerByName n)); \"\")");
        prog.update(100);
    }
    res.limbs = static_cast<int>(bones.size());
    res.lines.push_back(fmt(tr("ikrig_removed", L"IK rig removed, %1 bones keyed again"), {std::to_wstring(bones.size())}));
    GetCOREInterface()->RedrawViews(GetCOREInterface()->GetTime());
    return res;
}

} // namespace ikrig
} // namespace wdx
