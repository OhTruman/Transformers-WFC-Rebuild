// WFC fidelity harness — result registry + reporting.
// Every check carries the original-data provenance it is validating, so a failure points at
// the evidence, not just at a number.
//
// Status meanings:
//   PASS   rebuild matches the original (within tolerance).
//   FAIL   regression: a recovered/confirmed behaviour no longer holds. Non-zero exit.
//   KNOWN  documented deviation from the original, owned by another workstream. Does not fail
//          the run; flips to PASS ("RESOLVED") automatically once the owner fixes it.
//   INFO   measurement only (no confirmed original value yet). Feeds the compare step.
//   SKIP   prerequisite (e.g. extracted assets) unavailable.
#pragma once
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace fid {

enum class Status { Pass, Fail, Known, Info, Skip };

inline const char* statusName(Status s) {
    switch (s) {
        case Status::Pass:  return "PASS";
        case Status::Fail:  return "FAIL";
        case Status::Known: return "KNOWN";
        case Status::Info:  return "INFO";
        case Status::Skip:  return "SKIP";
    }
    return "?";
}

struct Result {
    std::string group, id;
    Status status = Status::Info;
    bool numeric = false;
    double measured = 0, expected = 0, tol = 0;
    std::string unit, source, owner, note;
};

class Report {
public:
    void setGroup(const std::string& g) { group_ = g; }

    // Confirmed original value: |measured - expected| <= tol, else regression.
    void near(const std::string& id, double measured, double expected, double tol,
              const std::string& unit, const std::string& source, const std::string& note = "") {
        Result r = num(id, measured, expected, tol, unit, source, note);
        r.status = std::fabs(measured - expected) <= tol ? Status::Pass : Status::Fail;
        add(r);
    }

    // Confirmed boolean behaviour.
    void truth(const std::string& id, bool ok, const std::string& source, const std::string& note = "") {
        Result r; r.id = id; r.source = source; r.note = note;
        r.status = ok ? Status::Pass : Status::Fail;
        add(r);
    }

    // Documented deviation: `expected` is the ORIGINAL behaviour. Reported, not failed.
    void known(const std::string& id, double measured, double expected, double tol,
               const std::string& unit, const std::string& source, const std::string& owner,
               const std::string& note) {
        Result r = num(id, measured, expected, tol, unit, source, note);
        r.owner = owner;
        if (std::fabs(measured - expected) <= tol) {
            r.status = Status::Pass;
            r.note = "RESOLVED (remove from known list): " + note;
        } else {
            r.status = Status::Known;
        }
        add(r);
    }
    void knownTruth(const std::string& id, bool originalBehaviourHolds, const std::string& source,
                    const std::string& owner, const std::string& note) {
        Result r; r.id = id; r.source = source; r.owner = owner; r.note = note;
        if (originalBehaviourHolds) { r.status = Status::Pass; r.note = "RESOLVED: " + note; }
        else r.status = Status::Known;
        add(r);
    }

    // Measurement without a confirmed original value (`expected` is the rebuild model's own
    // prediction when one exists; it is shown, never enforced).
    void info(const std::string& id, double measured, const std::string& unit, const std::string& note,
              double prediction = NAN) {
        Result r = num(id, measured, prediction, 0, unit, "", note);
        r.status = Status::Info;
        add(r);
    }

    void skip(const std::string& id, const std::string& why) {
        Result r; r.id = id; r.note = why; r.status = Status::Skip;
        add(r);
    }

    const std::vector<Result>& results() const { return results_; }
    int count(Status s) const {
        int n = 0;
        for (const Result& r : results_) n += r.status == s;
        return n;
    }

    // Last numeric value recorded under a fully-qualified id ("group.id").
    bool metric(const std::string& fq, double& out) const {
        auto it = metrics_.find(fq);
        if (it == metrics_.end()) return false;
        out = it->second;
        return true;
    }

    void printText(std::FILE* f, bool verbose) const {
        std::string g;
        for (const Result& r : results_) {
            if (!verbose && r.status == Status::Pass) continue;
            if (r.group != g) { g = r.group; std::fprintf(f, "\n[%s]\n", g.c_str()); }
            std::fprintf(f, "  %-5s %-44s", statusName(r.status), r.id.c_str());
            if (r.numeric) {
                std::fprintf(f, " %10.4f %-5s", r.measured, r.unit.c_str());
                if (!std::isnan(r.expected)) {
                    if (r.status == Status::Info) std::fprintf(f, " (model %.4f)", r.expected);
                    else std::fprintf(f, " (orig %.4f +-%.4g)", r.expected, r.tol);
                }
            }
            if (!r.owner.empty()) std::fprintf(f, " owner=%s", r.owner.c_str());
            std::fprintf(f, "\n");
            if (!r.note.empty()) std::fprintf(f, "        %s\n", r.note.c_str());
            if (verbose && !r.source.empty()) std::fprintf(f, "        src: %s\n", r.source.c_str());
        }
        std::fprintf(f, "\nSUMMARY: %d pass, %d FAIL, %d known-deviation, %d info, %d skip\n",
                     count(Status::Pass), count(Status::Fail), count(Status::Known),
                     count(Status::Info), count(Status::Skip));
    }

    bool writeJson(const std::string& path) const {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "{\n \"summary\": {\"pass\": %d, \"fail\": %d, \"known\": %d, \"info\": %d, \"skip\": %d},\n \"results\": [\n",
                     count(Status::Pass), count(Status::Fail), count(Status::Known),
                     count(Status::Info), count(Status::Skip));
        for (size_t i = 0; i < results_.size(); ++i) {
            const Result& r = results_[i];
            std::fprintf(f, "  {\"id\": \"%s.%s\", \"status\": \"%s\"", esc(r.group).c_str(), esc(r.id).c_str(),
                         statusName(r.status));
            if (r.numeric) {
                std::fprintf(f, ", \"measured\": %.6g", r.measured);
                if (!std::isnan(r.expected)) std::fprintf(f, ", \"expected\": %.6g, \"tol\": %.6g", r.expected, r.tol);
                std::fprintf(f, ", \"unit\": \"%s\"", esc(r.unit).c_str());
            }
            if (!r.owner.empty()) std::fprintf(f, ", \"owner\": \"%s\"", esc(r.owner).c_str());
            if (!r.source.empty()) std::fprintf(f, ", \"source\": \"%s\"", esc(r.source).c_str());
            if (!r.note.empty()) std::fprintf(f, ", \"note\": \"%s\"", esc(r.note).c_str());
            std::fprintf(f, "}%s\n", i + 1 < results_.size() ? "," : "");
        }
        std::fprintf(f, " ]\n}\n");
        std::fclose(f);
        return true;
    }

private:
    Result num(const std::string& id, double m, double e, double tol, const std::string& unit,
               const std::string& source, const std::string& note) {
        Result r; r.id = id; r.numeric = true; r.measured = m; r.expected = e; r.tol = tol;
        r.unit = unit; r.source = source; r.note = note;
        return r;
    }
    void add(Result r) {
        r.group = group_;
        if (r.numeric) metrics_[r.group + "." + r.id] = r.measured;
        results_.push_back(std::move(r));
    }
    static std::string esc(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else o += c;
        }
        return o;
    }

    std::string group_ = "misc";
    std::vector<Result> results_;
    std::map<std::string, double> metrics_;
};

} // namespace fid
