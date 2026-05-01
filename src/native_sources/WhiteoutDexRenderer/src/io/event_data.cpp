// ============================================================================
// EventObject SLK lookup — implementation.
// ============================================================================

#include "event_data.h"
#include "content_provider.h"
#include "slk.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

namespace WhiteoutDex::io {

namespace {

// File-local helpers paralleling replaceable_paths.cpp's. Kept here
// rather than promoted into a shared header because both files have
// their own per-table column probing and the helper signatures are
// trivial.

void LogHeaders(const char* /*label*/, const SlkTable& /*t*/) {
    // Header dump suppressed — re-enable for SLK schema diagnosis.
}

int FindAny(const SlkTable& t, std::initializer_list<const char*> names) {
    for (auto* n : names) { int c = t.FindColumn(n); if (c >= 0) return c; }
    return -1;
}

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

float ParseFloat(std::string_view sv) {
    if (sv.empty()) return 0.0f;
    return std::strtof(std::string(sv).c_str(), nullptr);
}

int ParseInt(std::string_view sv) {
    if (sv.empty()) return 0;
    return (int)std::strtol(std::string(sv).c_str(), nullptr, 10);
}

// Comma-split — AnimSounds.FileNames is a comma-delimited list of WAV
// stems within the row's Filepath directory.
std::vector<std::string> SplitComma(std::string_view sv) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= sv.size(); ++i) {
        if (i == sv.size() || sv[i] == ',') {
            if (i > start) {
                std::string_view part = sv.substr(start, i - start);
                while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
                while (!part.empty() && (part.back()  == ' ' || part.back()  == '\t')) part.remove_suffix(1);
                if (!part.empty()) out.emplace_back(part);
            }
            start = i + 1;
        }
    }
    return out;
}

// One copy per id, lower-cased so lookups match regardless of MDX
// authoring case. The SLK row's id column is treated case-insensitive.
struct DataCache {
    std::mutex mu;
    bool       loaded = false;
    std::unordered_map<std::string, SpnEntry> spn;
    std::unordered_map<std::string, SplEntry> spl;
    std::unordered_map<std::string, UbrEntry> ubr;
    std::unordered_map<std::string, SndEntry> snd;
};
DataCache& Cache() { static DataCache c; return c; }

void RewriteMdlToMdx(std::string& path) {
    if (path.size() >= 4) {
        auto tail = path.substr(path.size() - 4);
        for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
        if (tail == ".mdl") {
            path[path.size() - 1] = 'x';
        }
    }
}

void LoadSpawnData(IContentProvider& cp, DataCache& cache) {
    auto data = cp.ReadFile("Splats\\SpawnData.slk", nullptr);
    if (!data) { std::fprintf(stderr, "[events] ERR: SpawnData.slk: not found\n"); return; }
    SlkTable t = ParseSlk(*data);
    LogHeaders("SpawnData.slk", t);

    // Row key is positional (column 0). Real SpawnData.slk columns:
    // `Name Model version InBeta`, so col 0 is the per-effect id
    // (e.g. "GenericExplosion"). The table's named-column lookup
    // (FindAny) is only used for the *value* columns.
    const int colId    = 0;
    const int colModel = FindAny(t, {"Model", "model"});
    if (colModel < 0) {
        std::fprintf(stderr, "[events] ERR: SpawnData.slk missing Model col\n");
        return;
    }
    for (size_t r = 1; r < t.RowCount(); ++r) {
        std::string_view id    = t.Cell(r, colId);
        std::string_view model = t.Cell(r, colModel);
        if (id.empty() || model.empty()) continue;
        SpnEntry e;
        e.modelPath.assign(model);
        RewriteMdlToMdx(e.modelPath);
        cache.spn.emplace(ToLower(id), std::move(e));
    }
}

void LoadSplatData(IContentProvider& cp, DataCache& cache) {
    auto data = cp.ReadFile("Splats\\SplatData.slk", nullptr);
    if (!data) { std::fprintf(stderr, "[events] ERR: SplatData.slk: not found\n"); return; }
    SlkTable t = ParseSlk(*data);
    LogHeaders("SplatData.slk", t);

    // Row key is positional (column 0). Real SplatData.slk header is
    // `Name comment Dir file ...` — col 0 is the per-effect id.
    const int cId   = 0;
    const int cDir  = FindAny(t, {"Dir", "Texture"});      // directory column
    const int cFile = FindAny(t, {"file", "File", "Tex"}); // filename column (no .blp)
    const int cScale = FindAny(t, {"Scale"});
    const int cSR = FindAny(t, {"StartR"}), cSG = FindAny(t, {"StartG"}),
              cSB = FindAny(t, {"StartB"}), cSA = FindAny(t, {"StartA"});
    const int cMR = FindAny(t, {"MiddleR"}), cMG = FindAny(t, {"MiddleG"}),
              cMB = FindAny(t, {"MiddleB"}), cMA = FindAny(t, {"MiddleA"});
    const int cER = FindAny(t, {"EndR"}), cEG = FindAny(t, {"EndG"}),
              cEB = FindAny(t, {"EndB"}), cEA = FindAny(t, {"EndA"});
    const int cCols = FindAny(t, {"Columns"}), cRows = FindAny(t, {"Rows"});
    const int cLife = FindAny(t, {"Lifespan"}), cDecay = FindAny(t, {"Decay"});
    const int cUvLs = FindAny(t, {"UVLifespanStart"}), cUvLe = FindAny(t, {"UVLifespanEnd"});
    const int cLR   = FindAny(t, {"LifespanRepeat"});
    const int cUvDs = FindAny(t, {"UVDecayStart"}),    cUvDe = FindAny(t, {"UVDecayEnd"});
    const int cDR   = FindAny(t, {"DecayRepeat"});
    const int cBl   = FindAny(t, {"BlendMode"});

    for (size_t r = 1; r < t.RowCount(); ++r) {
        std::string_view id = t.Cell(r, cId);
        if (id.empty()) continue;
        SplEntry e;
        // The canonical splat texture path is
        // `ReplaceableTextures\Splats\<file>.blp` and the SLK's `Dir`
        // column is empty for stock content. We honour Dir when it is
        // non-empty so custom maps that drop splat textures into a
        // different folder still resolve.
        if (cFile >= 0) {
            std::string fn(t.Cell(r, cFile));
            if (cDir >= 0 && !t.Cell(r, cDir).empty()) {
                std::string dir(t.Cell(r, cDir));
                if (dir.back() != '\\' && dir.back() != '/') dir += '\\';
                e.file = dir + fn + ".blp";
            } else {
                e.file = "ReplaceableTextures\\Splats\\" + fn + ".blp";
            }
        }
        e.scale          = (cScale >= 0) ? ParseFloat(t.Cell(r, cScale)) : 1.0f;
        if (cSR >= 0) { e.startC[0]=ParseFloat(t.Cell(r,cSR))/255.f; e.startC[1]=ParseFloat(t.Cell(r,cSG))/255.f; e.startC[2]=ParseFloat(t.Cell(r,cSB))/255.f; e.startC[3]=ParseFloat(t.Cell(r,cSA))/255.f; }
        if (cMR >= 0) { e.midC[0]  =ParseFloat(t.Cell(r,cMR))/255.f; e.midC[1]  =ParseFloat(t.Cell(r,cMG))/255.f; e.midC[2]  =ParseFloat(t.Cell(r,cMB))/255.f; e.midC[3]  =ParseFloat(t.Cell(r,cMA))/255.f; }
        if (cER >= 0) { e.endC[0]  =ParseFloat(t.Cell(r,cER))/255.f; e.endC[1]  =ParseFloat(t.Cell(r,cEG))/255.f; e.endC[2]  =ParseFloat(t.Cell(r,cEB))/255.f; e.endC[3]  =ParseFloat(t.Cell(r,cEA))/255.f; }
        e.columns        = (cCols >= 0) ? std::max(1, ParseInt(t.Cell(r, cCols))) : 1;
        e.rows           = (cRows >= 0) ? std::max(1, ParseInt(t.Cell(r, cRows))) : 1;
        e.lifespan       = (cLife >= 0) ? ParseFloat(t.Cell(r, cLife)) : 0.0f;
        e.decay          = (cDecay >= 0) ? ParseFloat(t.Cell(r, cDecay)) : 0.0f;
        e.uvLifeStart    = (cUvLs >= 0) ? ParseInt(t.Cell(r, cUvLs)) : 0;
        e.uvLifeEnd      = (cUvLe >= 0) ? ParseInt(t.Cell(r, cUvLe)) : 0;
        e.lifespanRepeat = (cLR  >= 0) ? std::max(1, ParseInt(t.Cell(r, cLR))) : 1;
        e.uvDecayStart   = (cUvDs >= 0) ? ParseInt(t.Cell(r, cUvDs)) : 0;
        e.uvDecayEnd     = (cUvDe >= 0) ? ParseInt(t.Cell(r, cUvDe)) : 0;
        e.decayRepeat    = (cDR  >= 0) ? std::max(1, ParseInt(t.Cell(r, cDR))) : 1;
        e.blendMode      = (cBl  >= 0) ? ParseInt(t.Cell(r, cBl)) : 0;
        cache.spl.emplace(ToLower(id), std::move(e));
    }
}

void LoadUberSplatData(IContentProvider& cp, DataCache& cache) {
    auto data = cp.ReadFile("Splats\\UberSplatData.slk", nullptr);
    if (!data) { std::fprintf(stderr, "[events] ERR: UberSplatData.slk: not found\n"); return; }
    SlkTable t = ParseSlk(*data);
    LogHeaders("UberSplatData.slk", t);

    // Row key is positional (column 0), same as the other Splats SLKs.
    const int cId    = 0;
    const int cDir   = FindAny(t, {"Dir"});
    const int cFile  = FindAny(t, {"file", "File"});
    const int cScale = FindAny(t, {"Scale"});
    const int cSR = FindAny(t, {"StartR"}), cSG = FindAny(t, {"StartG"}),
              cSB = FindAny(t, {"StartB"}), cSA = FindAny(t, {"StartA"});
    const int cMR = FindAny(t, {"MiddleR"}), cMG = FindAny(t, {"MiddleG"}),
              cMB = FindAny(t, {"MiddleB"}), cMA = FindAny(t, {"MiddleA"});
    const int cER = FindAny(t, {"EndR"}), cEG = FindAny(t, {"EndG"}),
              cEB = FindAny(t, {"EndB"}), cEA = FindAny(t, {"EndA"});
    const int cBirth = FindAny(t, {"BirthTime"});
    const int cPause = FindAny(t, {"PauseTime"});
    const int cDecay = FindAny(t, {"Decay"});
    const int cBl    = FindAny(t, {"BlendMode"});
    for (size_t r = 1; r < t.RowCount(); ++r) {
        std::string_view id = t.Cell(r, cId);
        if (id.empty()) continue;
        UbrEntry e;
        if (cFile >= 0) {
            std::string fn(t.Cell(r, cFile));
            if (cDir >= 0 && !t.Cell(r, cDir).empty()) {
                std::string dir(t.Cell(r, cDir));
                if (dir.back() != '\\' && dir.back() != '/') dir += '\\';
                e.file = dir + fn + ".blp";
            } else {
                e.file = "ReplaceableTextures\\Splats\\" + fn + ".blp";
            }
        }
        e.scale = (cScale >= 0) ? ParseFloat(t.Cell(r, cScale)) : 1.0f;
        if (cSR >= 0) { e.c[0][0]=ParseFloat(t.Cell(r,cSR))/255.f; e.c[0][1]=ParseFloat(t.Cell(r,cSG))/255.f; e.c[0][2]=ParseFloat(t.Cell(r,cSB))/255.f; e.c[0][3]=ParseFloat(t.Cell(r,cSA))/255.f; }
        if (cMR >= 0) { e.c[1][0]=ParseFloat(t.Cell(r,cMR))/255.f; e.c[1][1]=ParseFloat(t.Cell(r,cMG))/255.f; e.c[1][2]=ParseFloat(t.Cell(r,cMB))/255.f; e.c[1][3]=ParseFloat(t.Cell(r,cMA))/255.f; }
        if (cER >= 0) { e.c[2][0]=ParseFloat(t.Cell(r,cER))/255.f; e.c[2][1]=ParseFloat(t.Cell(r,cEG))/255.f; e.c[2][2]=ParseFloat(t.Cell(r,cEB))/255.f; e.c[2][3]=ParseFloat(t.Cell(r,cEA))/255.f; }
        e.birthTime = (cBirth >= 0) ? ParseFloat(t.Cell(r, cBirth)) : 0.0f;
        e.pauseTime = (cPause >= 0) ? ParseFloat(t.Cell(r, cPause)) : 0.0f;
        e.decay     = (cDecay >= 0) ? ParseFloat(t.Cell(r, cDecay)) : 0.0f;
        e.blendMode = (cBl    >= 0) ? ParseInt(t.Cell(r, cBl)) : 0;
        cache.ubr.emplace(ToLower(id), std::move(e));
    }
}

// AnimSounds.slk — Reforged consolidated this so each row carries its
// own `AnimationEventCode` column (the 4-letter id from the SND
// EventObject's name suffix). Classic-era WC3 used a separate
// AnimLookups.slk to translate the code → SoundLabel, but that file
// doesn't exist in Reforged builds — verified against
// `Warcraft III.exe`'s string table: only `AnimSounds.slk` is
// referenced (string @ 0x142209ca8), `AnimLookups.slk` is not.
//
// Column names probed from the same binary's column-header constants
// (AnimationEventCode @ 0x142477318, FileNames @ 0x142477330, ...).
void LoadAnimSounds(IContentProvider& cp, DataCache& cache) {
    auto sounds = cp.ReadFile("UI\\SoundInfo\\AnimSounds.slk", nullptr);
    if (!sounds) {
        std::fprintf(stderr, "[events] ERR: AnimSounds.slk: not found\n");
        return;
    }
    SlkTable st = ParseSlk(*sounds);
    LogHeaders("AnimSounds.slk", st);

    // The row key is the AnimationEventCode column (e.g. "Hblm" for
    // BladeMaster). Fall back to col 0 if the header isn't present
    // (some cut-down builds drop the column name).
    int sCode = FindAny(st, {"AnimationEventCode", "EventCode", "Code"});
    if (sCode < 0) sCode = 0;
    const int sFiles = FindAny(st, {"FileNames", "Files", "filenames"});
    const int sPath  = FindAny(st, {"DirectoryBase", "Filepath", "filepath", "Directory"});
    const int sVol   = FindAny(st, {"Volume"});
    const int sMin   = FindAny(st, {"MinDistance"});
    const int sMax   = FindAny(st, {"MaxDistance"});
    const int sCut   = FindAny(st, {"DistanceCutoff"});

    for (size_t r = 1; r < st.RowCount(); ++r) {
        std::string_view code = st.Cell(r, sCode);
        if (code.empty()) continue;
        SndEntry e;
        if (sFiles >= 0) e.fileNames = SplitComma(st.Cell(r, sFiles));
        if (sPath  >= 0) e.filepath.assign(st.Cell(r, sPath));
        e.volume         = (sVol >= 0) ? ParseFloat(st.Cell(r, sVol)) : 1.0f;
        e.minDistance    = (sMin >= 0) ? ParseFloat(st.Cell(r, sMin)) : 0.0f;
        e.maxDistance    = (sMax >= 0) ? ParseFloat(st.Cell(r, sMax)) : 0.0f;
        e.distanceCutoff = (sCut >= 0) ? ParseFloat(st.Cell(r, sCut)) : 0.0f;
        cache.snd.emplace(ToLower(code), std::move(e));
    }
}

template <class Map>
const typename Map::mapped_type* FindIn(const Map& m, std::string_view id) {
    if (id.empty()) return nullptr;
    auto key = ToLower(id);
    auto it = m.find(key);
    return (it == m.end()) ? nullptr : &it->second;
}

} // namespace

void LoadEventDataFiles(IContentProvider* cp, bool force) {
    if (!cp) return;
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    if (c.loaded && !force) return;
    c.spn.clear(); c.spl.clear(); c.ubr.clear(); c.snd.clear();
    LoadSpawnData    (*cp, c);
    LoadSplatData    (*cp, c);
    LoadUberSplatData(*cp, c);
    LoadAnimSounds   (*cp, c);
    c.loaded = true;
}

const SpnEntry* FindSpn(std::string_view id) {
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    return FindIn(c.spn, id);
}
const SplEntry* FindSpl(std::string_view id) {
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    return FindIn(c.spl, id);
}
const UbrEntry* FindUbr(std::string_view id) {
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    return FindIn(c.ubr, id);
}
const SndEntry* FindSnd(std::string_view id) {
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    return FindIn(c.snd, id);
}

} // namespace WhiteoutDex::io
