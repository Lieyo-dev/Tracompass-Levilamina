#include "tracompass/Data.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace tracompass::data {

namespace {
std::vector<TrackerEntry> gTrackers;
std::filesystem::path     gFile;

std::vector<std::string> split(std::string const& line) {
    std::vector<std::string> parts;
    std::stringstream        ss(line);
    std::string              item;
    while (std::getline(ss, item, '\t')) parts.push_back(item);
    return parts;
}
} // namespace

void load(std::filesystem::path const& dataDir) {
    gFile = dataDir / "trackers.txt";
    gTrackers.clear();
    std::ifstream in(gFile);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto p = split(line);
        // T <uuid> <name>
        if (p.size() == 3 && p[0] == "T") {
            addTracker({mce::UUID::fromString(p[1]), p[2]});
        }
        // A <trackerUuid> <targetUuid> <targetName>
        else if (p.size() == 4 && p[0] == "A") {
            if (auto* t = findTracker(mce::UUID::fromString(p[1]))) {
                t->targets.push_back({mce::UUID::fromString(p[2]), p[3]});
            }
        }
    }
}

void save() {
    std::error_code ec;
    std::filesystem::create_directories(gFile.parent_path(), ec);
    std::ofstream out(gFile, std::ios::trunc);
    for (auto& t : gTrackers) {
        out << "T\t" << t.self.uuid.asString() << '\t' << t.self.name << '\n';
        for (auto& target : t.targets)
            out << "A\t" << t.self.uuid.asString() << '\t' << target.uuid.asString() << '\t' << target.name << '\n';
    }
}

std::vector<TrackerEntry> const& trackers() { return gTrackers; }

TrackerEntry* findTracker(mce::UUID const& uuid) {
    for (auto& t : gTrackers)
        if (t.self.uuid == uuid) return &t;
    return nullptr;
}

bool addTracker(PlayerRef const& tracker) {
    if (findTracker(tracker.uuid)) return false;
    gTrackers.push_back({tracker, {}});
    return true;
}

bool removeTracker(mce::UUID const& tracker) {
    auto it = std::find_if(gTrackers.begin(), gTrackers.end(), [&](auto& t) { return t.self.uuid == tracker; });
    if (it == gTrackers.end()) return false;
    gTrackers.erase(it);
    return true;
}

bool allow(PlayerRef const& tracker, PlayerRef const& target) {
    addTracker(tracker);
    auto* t = findTracker(tracker.uuid);
    if (!t || isAllowed(tracker.uuid, target.uuid)) return false;
    t->targets.push_back(target);
    return true;
}

bool deny(mce::UUID const& tracker, mce::UUID const& target) {
    auto* t = findTracker(tracker);
    if (!t) return false;
    auto it = std::find_if(t->targets.begin(), t->targets.end(), [&](auto& r) { return r.uuid == target; });
    if (it == t->targets.end()) return false;
    t->targets.erase(it);
    return true;
}

bool isAllowed(mce::UUID const& tracker, mce::UUID const& target) {
    auto* t = findTracker(tracker);
    if (!t) return false;
    return std::any_of(t->targets.begin(), t->targets.end(), [&](auto& r) { return r.uuid == target; });
}

} // namespace tracompass::data
